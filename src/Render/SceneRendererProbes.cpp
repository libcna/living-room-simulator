// SPDX-License-Identifier: MIT
// Interior probe capture, convolution and incremental rebaking.
#include "CnaRoom/Render/SceneRenderer.hpp"

#include "CnaRoom/Render/FloatCube.hpp"
#include "CnaRoom/Render/GpuMesh.hpp"
#include "CnaRoom/Render/Irradiance.hpp"
#include "CnaRoom/Render/Material.hpp"
#include "CnaRoom/Sim/TimeOfDay.hpp"
#include "CnaRoom/Effects/EnvironmentProcessor.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "System/Diagnostics/Stopwatch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using System::Diagnostics::Stopwatch;

namespace CnaRoom {

namespace {
float milliseconds(const Stopwatch& watch)
{
    return static_cast<float>(watch.getElapsedTicksProperty()) / 10000.0f;
}
}  // namespace

void SceneRenderer::assignProbes()
{
    for (SceneItem& item : items_)
    {
        item.probe = nullptr;
        if (item.exterior || probes_.empty()) continue;
        float best = 1e30f;
        for (const auto& probe : probes_)
        {
            const float d = Vector3::DistanceSquared(probe->position, item.worldSphere.Center);
            if (d < best)
            {
                best = d;
                item.probe = probe.get();
            }
        }
    }
}

namespace {

float decodeSrgb(int value)
{
    const float c = static_cast<float>(value) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

std::uint8_t encodeCube(float linear, float scale)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(linear / std::max(scale, 1e-6f), 0.0f, 1.0f) * 255.0f));
}

/// The camera basis that renders cube face `face` so that texel (u, v) of the
/// readback (after the detected mirror/flip) sees EnvironmentProcessor::faceDirection(face, u, v).
void faceBasis(int face, Vector3& forward, Vector3& up)
{
    using CnaRoom::Effects::EnvironmentProcessor;
    forward = EnvironmentProcessor::faceDirection(face, 0.5f, 0.5f);
    const Vector3 down = EnvironmentProcessor::faceDirection(face, 0.5f, 1.0f)
                         - EnvironmentProcessor::faceDirection(face, 0.5f, 0.0f);
    up = -down;
    const float l = up.Length();
    if (l > 1e-5f) up = up * (1.0f / l);
}

}  // namespace

void SceneRenderer::captureProbeFace(InteriorProbe& probe, RenderTarget2D& target, int size,
                                     const RenderSettings& settings, bool skyOnly, int f,
                                     std::vector<Vector3>& faces, float& peak)
{
    // Into a half-float target the radiance is read back as it is. The 8-bit
    // fallback captures at half brightness so sunlit surfaces near 2.0 still
    // fit, with the effect's sRGB encode keeping the dark end in more than a
    // few steps; both are undone when the texel goes into the cube.
    lightScale_ = captureHdr_ ? 1.0f : 0.5f;
    capturing_ = true;
    appliedLamp_ = -2;   // re-apply the lamp under the capture's rules (the portals are left out)
    usingSceneTarget_ = false;
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (faces.size() != 6u * count) faces.assign(6u * count, Vector3::Zero);
    std::vector<Color> captured(captureHdr_ ? 0u : count, Color::Black);
    std::vector<PackedVector::HalfVector4> capturedHdr(captureHdr_ ? count : 0u);

    Vector3 forward, up;
    faceBasis(f, forward, up);
    const Matrix view = Matrix::CreateLookAt(probe.position, probe.position + forward, up);
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver2, 1.0f, 0.05f, 300.0f);
    const BoundingFrustum frustum(view * projection);

    device_.SetRenderTarget(&target);
    device_.Clear(Color::Black, 1.0f);
    device_.setDepthStencilStateProperty(DepthStencilState::None);
    device_.setBlendStateProperty(BlendState::Opaque);
    if (settings.sky) sky_.draw(view, projection, size, size, !captureHdr_, lightScale_);
    if (!skyOnly)
    {
        device_.setDepthStencilStateProperty(DepthStencilState::Default);
        applyLighting(settings);
        // Shadows: the cascades were fitted to the viewing camera; the
        // receiver picks a cascade by depth along that camera, so the
        // capture keeps whatever the last frame fitted (correct for the
        // first cascade's neighbourhood, unshadowed beyond it).
        for (const SceneItem& item : items_)
        {
            if (item.material->isBlended() || item.shadowOnly) continue;
            if (frustum.Contains(item.worldSphere) == ContainmentType::Disjoint) continue;
            applyMaterial(*item.material, item.world, view, projection, item.probe, item.lamp);
            item.mesh->draw(device_);
        }
    }
    device_.SetRenderTarget(nullptr);
    if (captureHdr_) target.GetData(capturedHdr.data(), static_cast<int>(count));
    else target.GetData(captured.data(), static_cast<int>(count));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const int sx = probeMirrorX_ ? size - 1 - x : x;
            const int sy = probeFlipY_ ? size - 1 - y : y;
            const std::size_t index = static_cast<std::size_t>(sy) * static_cast<std::size_t>(size) + static_cast<std::size_t>(sx);
            Vector3 radiance;
            if (captureHdr_)
            {
                const Vector4 v = capturedHdr[index].ToVector4();
                radiance = Vector3(std::max(v.X, 0.0f), std::max(v.Y, 0.0f), std::max(v.Z, 0.0f));
                if (!std::isfinite(v.X) || !std::isfinite(v.Y) || !std::isfinite(v.Z))
                {
                    // The current EasyGL PBR scene can contain non-finite samples (CNA_FINDINGS.md).
                    // Keep one bad texel from poisoning the complete irradiance convolution.
                    radiance = Vector3::Zero;
                    int samples = 0;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            const int nx = std::clamp(sx + dx, 0, size - 1);
                            const int ny = std::clamp(sy + dy, 0, size - 1);
                            const auto neighbour = capturedHdr[static_cast<std::size_t>(ny) * size + nx].ToVector4();
                            if (!std::isfinite(neighbour.X) || !std::isfinite(neighbour.Y) || !std::isfinite(neighbour.Z)) continue;
                            radiance += Vector3(std::max(neighbour.X, 0.0f), std::max(neighbour.Y, 0.0f), std::max(neighbour.Z, 0.0f));
                            ++samples;
                        }
                    if (samples > 0) radiance *= 1.0f / static_cast<float>(samples);
                }
            }
            else
            {
                const Color& pixel = captured[index];
                radiance = Vector3(decodeSrgb(pixel.getRProperty()) / lightScale_,
                                   decodeSrgb(pixel.getGProperty()) / lightScale_,
                                   decodeSrgb(pixel.getBProperty()) / lightScale_);
            }
            faces[static_cast<std::size_t>(f) * count + static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] = radiance;
            peak = std::max(peak, std::max(radiance.X, std::max(radiance.Y, radiance.Z)));
        }
    lightScale_ = 1.0f;
    capturing_ = false;
    appliedLamp_ = -2;
    appliedMaterial_ = nullptr;
    environmentBound_ = false;
}

std::unique_ptr<TextureCube> SceneRenderer::integrateIrradiance(const std::vector<Vector3>& faces, int size, float scale,
                                                                 float gain)
{
    constexpr int kSmall = 8, kOut = 16;
    // Exact cosine convolution over an 8x8-per-face downsample, stored as
    // E / pi relative to the probe's scale (the shader multiplies by it).
    // The bounce gain stands in for the bounces the capture cannot hold.
    std::vector<Vector3> irradiance = convolveIrradiance(faces, size, kSmall, kOut);
    const float invScale = gain / std::max(scale, 1e-6f);
    lastIrradianceMean_ = Vector3::Zero;
    lastIrradianceSize_ = kOut;
    lastIrradianceStrip_.assign(static_cast<std::size_t>(6 * kOut) * kOut * 4u, 255);
    const auto encodeSrgb8 = [](float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        value = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        return static_cast<int>(std::lround(value * 255.0f));
    };
    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < kOut; ++y)
            for (int x = 0; x < kOut; ++x)
            {
                Vector3& e = irradiance[static_cast<std::size_t>(f * kOut * kOut + y * kOut + x)];
                e = e * invScale;
                lastIrradianceMean_ = lastIrradianceMean_ + e * (1.0f / static_cast<float>(6 * kOut * kOut));
                const std::size_t stripIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(6 * kOut) + static_cast<std::size_t>(f * kOut + x)) * 4u;
                lastIrradianceStrip_[stripIndex] = static_cast<std::uint8_t>(encodeSrgb8(e.X * scale));
                lastIrradianceStrip_[stripIndex + 1] = static_cast<std::uint8_t>(encodeSrgb8(e.Y * scale));
                lastIrradianceStrip_[stripIndex + 2] = static_cast<std::uint8_t>(encodeSrgb8(e.Z * scale));
            }

    // Half-float cube when the renderer can fill one (CNA_FINDINGS R-21): at
    // night the diffuse ambient is ~1/300 of the shade peak that sets the
    // scale, below one 8-bit code.
    if (floatCubes_ != nullptr)
    {
        if (std::unique_ptr<RenderTargetCube> cube = floatCubes_->upload(irradiance, kOut)) return cube;
    }
    // Fallback: 8-bit, sRGB-encoded when the format exists (it does not on EasyGL).
    std::unique_ptr<TextureCube> cube;
    bool srgb = true;
    try
    {
        cube = std::make_unique<TextureCube>(device_, kOut, false, SurfaceFormat::ColorSrgbEXT);
    }
    catch (const std::exception&)
    {
        srgb = false;
        cube = std::make_unique<TextureCube>(device_, kOut, false, SurfaceFormat::Color);
    }
    std::vector<Color> out(static_cast<std::size_t>(kOut) * kOut);
    for (int f = 0; f < 6; ++f)
    {
        for (int y = 0; y < kOut; ++y)
            for (int x = 0; x < kOut; ++x)
            {
                const Vector3& e = irradiance[static_cast<std::size_t>(f * kOut * kOut + y * kOut + x)];
                const auto encode = [&](float value) {
                    value = std::clamp(value, 0.0f, 1.0f);
                    if (srgb) value = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
                    return static_cast<int>(std::lround(value * 255.0f));
                };
                out[static_cast<std::size_t>(y) * kOut + static_cast<std::size_t>(x)] = Color(encode(e.X), encode(e.Y), encode(e.Z), 255);
            }
        cube->SetData(static_cast<CubeMapFace>(f), out.data(), static_cast<int>(out.size()));
    }
    if (!srgb && !loggedIrradianceFormat_)
    {
        loggedIrradianceFormat_ = true;
        limitations_.emplace_back("irradiance cubes are linear 8-bit (no sRGB cube format, no half-float cube upload: "
                                  + (floatCubes_ != nullptr ? floatCubes_->reason() : std::string("no uploader")) + "): dark rooms quantise");
    }
    return cube;
}

void SceneRenderer::finishProbe(InteriorProbe& probe, int size, const std::vector<Vector3>& faces, float peak,
                                const RenderSettings& settings)
{
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    // A fresh cube every time: the previous one may still be bound as the
    // effect's environment while this runs, and EasyGL cannot read a bound
    // cube back for the irradiance integration (it stays valid until the
    // next draw binds the new one).
    probe.environment = std::make_unique<TextureCube>(device_, size, false, SurfaceFormat::Color);
    probe.scale = std::max(peak, 1e-4f) * 1.02f;
    std::vector<Color> face(count);
    for (int f = 0; f < 6; ++f)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const Vector3& r = faces[static_cast<std::size_t>(f) * count + i];
            face[i] = Color(static_cast<int>(encodeCube(r.X, probe.scale)), static_cast<int>(encodeCube(r.Y, probe.scale)),
                            static_cast<int>(encodeCube(r.Z, probe.scale)), 255);
        }
        probe.environment->SetData(static_cast<CubeMapFace>(f), face.data(), static_cast<int>(count));
    }
    // Irradiance: integrated here from the float capture (no sampling noise,
    // no 8-bit input) over an 8x8-per-face downsample, then stored sRGB-encoded
    // so the dark end keeps its precision under the shared scale. The bounce
    // gain stands in for the bounces the capture cannot hold.
    CnaRoom::Effects::EnvironmentProcessor processor(device_);
    const float gain = std::clamp(settings.probeBounceGain, 1.0f, 3.0f);
    probe.irradiance = integrateIrradiance(faces, size, probe.scale, gain);
    {
        // The mean irradiance (E / pi, scene units) says what colour the
        // probe paints on a white surface: a neutral room stays near grey.
        const Vector3 m = lastIrradianceMean_ * probe.scale;
        probe.meanIrradiance = m;
        const float lum = std::max(0.2126f * m.X + 0.7152f * m.Y + 0.0722f * m.Z, 1e-9f);
        CNA::Logger::Info("living-room-simulator: probe at " + std::to_string(probe.position.X) + "," + std::to_string(probe.position.Y) + ","
                          + std::to_string(probe.position.Z) + " mean irradiance " + std::to_string(m.X) + "," + std::to_string(m.Y) + ","
                          + std::to_string(m.Z) + " (chroma " + std::to_string(m.X / lum) + "," + std::to_string(m.Y / lum) + ","
                          + std::to_string(m.Z / lum) + "), peak " + std::to_string(peak));
    }
    if (!probeDumpDirectory_.empty()) dumpProbe(probe, size, faces);
    // Specular from the environment cube through CNA (CNA_FINDINGS R-14).
    probe.prefiltered = processor.generatePrefilteredSpecular(probe.environment.get(), std::min(size, 32), probe.prefilteredMips, 24);
    // And in half-float, one cube per roughness class from the float faces:
    // the 8-bit chain quantises a night room's reflection to a few codes.
    probe.prefilteredClasses.clear();
    if (floatCubes_ != nullptr && floatCubes_->supported() && settings.specularClasses)
    {
        const Stopwatch classWatch = Stopwatch::StartNew();
        const float invScale = 1.0f / std::max(probe.scale, 1e-6f);
        bool complete = true;
        for (const float roughness : kSpecularClasses)
        {
            std::vector<Vector3> filtered = prefilterSpecular(faces, size, 32, roughness, 32);
            for (Vector3& v : filtered) v = v * invScale;
            std::unique_ptr<RenderTargetCube> cube = floatCubes_->upload(filtered, 32);
            if (cube == nullptr) complete = false;
            probe.prefilteredClasses.push_back(std::move(cube));
        }
        if (!complete) probe.prefilteredClasses.clear();
        if (!loggedSpecularClasses_)
        {
            loggedSpecularClasses_ = true;
            CNA::Logger::Info("living-room-simulator: probe specular classes (" + std::to_string(kSpecularClasses.size()) + " x 32 px, 32 samples) in "
                              + std::to_string(static_cast<double>(classWatch.getElapsedTicksProperty()) / 10000.0) + " ms"
                              + (complete ? "" : " -- upload failed, CNA's 8-bit chain used"));
        }
    }
    device_.SetRenderTarget(nullptr);
    environmentBound_ = false;
    appliedProbe_ = nullptr;
}

void SceneRenderer::dumpProbe(const InteriorProbe& /*probe*/, int size, const std::vector<Vector3>& faces)
{
    // Captured faces as a strip (+X -X +Y -Y +Z -Z), exposed so the mean
    // luminance lands on mid grey, plus the irradiance strip at the cube's
    // own scale. For reading a tint or a missing surface, not a picture.
    try
    {
        std::filesystem::create_directories(probeDumpDirectory_);
        const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
        double sum = 0.0;
        for (const Vector3& r : faces) sum += 0.2126 * r.X + 0.7152 * r.Y + 0.0722 * r.Z;
        const float exposure = 0.18f / std::max(static_cast<float>(sum / static_cast<double>(faces.size())), 1e-6f);
        const auto encode = [](float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            value = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint8_t>(std::lround(value * 255.0f));
        };
        std::vector<std::uint8_t> strip(static_cast<std::size_t>(6 * size) * static_cast<std::size_t>(size) * 4u, 255);
        for (int f = 0; f < 6; ++f)
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    const Vector3& r = faces[static_cast<std::size_t>(f) * count + static_cast<std::size_t>(y) * static_cast<std::size_t>(size)
                                             + static_cast<std::size_t>(x)];
                    const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(6 * size) + static_cast<std::size_t>(f * size + x)) * 4u;
                    // Reinhard keeps the shades from clipping to a flat white.
                    const auto tone = [&](float v) { v *= exposure; return v / (1.0f + v); };
                    strip[o] = encode(tone(r.X));
                    strip[o + 1] = encode(tone(r.Y));
                    strip[o + 2] = encode(tone(r.Z));
                }
        const std::string stem = probeDumpDirectory_ + "/probe" + std::to_string(probeDumpCount_);
        Texture2D env = Texture2D::CreateFromPixels(device_, 6 * size, size, strip);
        env.SaveAsPng(stem + "-env.png");
        if (lastIrradianceSize_ > 0)
        {
            const int n = lastIrradianceSize_;
            Texture2D image = Texture2D::CreateFromPixels(device_, 6 * n, n, lastIrradianceStrip_);
            image.SaveAsPng(stem + "-irr.png");
        }
        CNA::Logger::Info("living-room-simulator: probe dump " + stem + "-{env,irr}.png (exposure " + std::to_string(exposure) + ")");
        ++probeDumpCount_;
    }
    catch (const std::exception& error)
    {
        CNA::Logger::Warn(std::string("living-room-simulator: probe dump failed: ") + error.what());
    }
}

void SceneRenderer::captureProbe(InteriorProbe& probe, RenderTarget2D& target, int size,
                                 const RenderSettings& settings, bool skyOnly)
{
    std::vector<Vector3> faces;
    float peak = 1e-4f;
    for (int f = 0; f < 6; ++f) captureProbeFace(probe, target, size, settings, skyOnly, f, faces, peak);
    // Environment only (the mapping detection reads it back); no products.
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (probe.environment == nullptr || probe.environment->getSizeProperty() != size)
        probe.environment = std::make_unique<TextureCube>(device_, size, false, SurfaceFormat::Color);
    probe.scale = peak * 1.02f;
    std::vector<Color> face(count);
    for (int f = 0; f < 6; ++f)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const Vector3& r = faces[static_cast<std::size_t>(f) * count + i];
            face[i] = Color(static_cast<int>(encodeCube(r.X, probe.scale)), static_cast<int>(encodeCube(r.Y, probe.scale)),
                            static_cast<int>(encodeCube(r.Z, probe.scale)), 255);
        }
        probe.environment->SetData(static_cast<CubeMapFace>(f), face.data(), static_cast<int>(count));
    }
}

void SceneRenderer::detectCaptureFormat()
{
    // A half-float target keeps the sun disc and lamp glows above 2.0 in the
    // probe; use it when the device can render into one and read it back.
    captureFormatKnown_ = true;
    captureHdr_ = false;
    try
    {
        RenderTarget2D test(device_, 4, 4, false, SurfaceFormat::HalfVector4, DepthFormat::None);
        device_.SetRenderTarget(&test);
        device_.Clear(Color(255, 128, 0, 255));
        device_.SetRenderTarget(nullptr);
        std::array<PackedVector::HalfVector4, 16> pixels{};
        test.GetData(pixels.data(), 16);
        const Vector4 v = pixels[5].ToVector4();
        captureHdr_ = std::abs(v.X - 1.0f) < 0.01f && std::abs(v.Y - 128.0f / 255.0f) < 0.01f && std::abs(v.Z) < 0.01f;
    }
    catch (const std::exception& failure)
    {
        limitations_.emplace_back(std::string("8-bit probe capture: ") + failure.what());
    }
    if (!captureHdr_ && limitations_.empty())
        limitations_.emplace_back("8-bit probe capture: half-float readback gave wrong values");
    CNA::Logger::Info(std::string("living-room-simulator: probe capture ") + (captureHdr_ ? "half-float" : "8-bit sRGB, radiance clipped at 2.0"));
}

void SceneRenderer::detectProbeMapping(RenderTarget2D& target, int size, const RenderSettings& settings)
{
    // Render the sky alone from the origin and find which of the four
    // readback orientations reproduces the CPU sky per texel.
    InteriorProbe test;
    test.position = Vector3(0.0f, 50.0f, 0.0f);
    double bestError = 1e30;
    bool bestMirror = true, bestFlip = false;
    for (int mirror = 0; mirror < 2; ++mirror)
        for (int flip = 0; flip < 2; ++flip)
        {
            probeMirrorX_ = mirror == 1;
            probeFlipY_ = flip == 1;
            captureProbe(test, target, size, settings, true);
            std::vector<Color> texels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
            double error = 0.0;
            int samples = 0;
            for (int f = 0; f < 6; ++f)
            {
                test.environment->GetData(static_cast<CubeMapFace>(f), texels.data(), static_cast<int>(texels.size()));
                for (int y = 0; y < size; y += 4)
                    for (int x = 0; x < size; x += 4)
                    {
                        const Vector3 expected = sky_.radiance(CnaRoom::Effects::EnvironmentProcessor::faceDirection(
                            f, (static_cast<float>(x) + 0.5f) / static_cast<float>(size), (static_cast<float>(y) + 0.5f) / static_cast<float>(size)));
                        const Color& c = texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)];
                        const Vector3 got(static_cast<float>(c.getRProperty()) / 255.0f * test.scale,
                                          static_cast<float>(c.getGProperty()) / 255.0f * test.scale,
                                          static_cast<float>(c.getBProperty()) / 255.0f * test.scale);
                        error += static_cast<double>(Vector3::Distance(expected, got));
                        ++samples;
                    }
            }
            error /= std::max(samples, 1);
            if (error < bestError)
            {
                bestError = error;
                bestMirror = probeMirrorX_;
                bestFlip = probeFlipY_;
            }
            CNA::Logger::Info("living-room-simulator: probe mapping mirrorX=" + std::to_string(probeMirrorX_) + " flipY="
                              + std::to_string(probeFlipY_) + " error " + std::to_string(error));
        }
    probeMirrorX_ = bestMirror;
    probeFlipY_ = bestFlip;
    probeMappingKnown_ = true;
}

void SceneRenderer::requestProbeBake(const std::vector<Vector3>& positions, const RenderSettings& settings,
                                     int iterations)
{
    probeBakeQueue_.clear();
    if (!settings.interiorProbes || !device_.SupportsImageBasedLightingEXT() || sky_.brdfLut() == nullptr)
    {
        probes_.clear();
        assignProbes();
        return;
    }
    currentSettings_ = &settings;
    probeBakeSize_ = std::max(16, settings.probeFaceSize);
    if (!captureFormatKnown_) detectCaptureFormat();
    if (probeTarget_ == nullptr || probeTarget_->getWidthProperty() != probeBakeSize_)
        probeTarget_ = std::make_unique<RenderTarget2D>(device_, probeBakeSize_, probeBakeSize_, false,
                                                        captureHdr_ ? SurfaceFormat::HalfVector4 : SurfaceFormat::Color,
                                                        DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
    if (!probeMappingKnown_) detectProbeMapping(*probeTarget_, probeBakeSize_, settings);

    if (probes_.size() != positions.size())
    {
        probes_.clear();
        for (const Vector3& p : positions)
        {
            auto probe = std::make_unique<InteriorProbe>();
            probe->position = p;
            probes_.push_back(std::move(probe));
        }
        assignProbes();
    }
    assignLamps();
    for (int iteration = 0; iteration < std::max(1, iterations); ++iteration)
        for (std::size_t p = 0; p < probes_.size(); ++p)
            for (int f = 0; f < 6; ++f)
                probeBakeQueue_.push_back(ProbeBakeStep{p, f});
    probeBakeFaces_.clear();
    probeBakePeak_ = 1e-4f;
    probeBakeWatch_ = Stopwatch::StartNew();
    probeBakeStepsDone_ = 0;
    probeBakeQueueTotal_ = probeBakeQueue_.size();
}

int SceneRenderer::probeBakeFacesPerFrame() const
{
    return bakeFacesPerFrame(probeBakeQueueTotal_, probeBakeMinutesPerFrame_);
}

bool SceneRenderer::stepProbeBake(int faces)
{
    if (probeBakeQueue_.empty() || probeTarget_ == nullptr || currentSettings_ == nullptr) return false;
    for (int i = 0; i < std::max(1, faces) && !probeBakeQueue_.empty(); ++i)
    {
        const ProbeBakeStep step = probeBakeQueue_.front();
        probeBakeQueue_.erase(probeBakeQueue_.begin());
        InteriorProbe& probe = *probes_[step.probe];
        captureProbeFace(probe, *probeTarget_, probeBakeSize_, *currentSettings_, false, step.face, probeBakeFaces_, probeBakePeak_);
        ++probeBakeStepsDone_;
        if (step.face == 5)
        {
            finishProbe(probe, probeBakeSize_, probeBakeFaces_, probeBakePeak_, *currentSettings_);
            probeBakePeak_ = 1e-4f;
        }
    }
    if (probeBakeQueue_.empty())
    {
        probeBakeSeconds_ = milliseconds(probeBakeWatch_) / 1000.0f;
        ++probeBakeCount_;
        if (probeBakeCount_ <= 3)
        {
            for (const auto& probe : probes_)
                CNA::Logger::Info("living-room-simulator: probe at " + std::to_string(probe->position.X) + "," + std::to_string(probe->position.Y)
                                  + "," + std::to_string(probe->position.Z) + " peak radiance " + std::to_string(probe->scale));
            CNA::Logger::Info("living-room-simulator: baked " + std::to_string(probes_.size()) + " interior probes at "
                              + std::to_string(probeBakeSize_) + " px, " + std::to_string(probeBakeStepsDone_) + " faces, in "
                              + std::to_string(probeBakeSeconds_) + " s");
        }
        return false;
    }
    return true;
}

void SceneRenderer::bakeInteriorProbes(const std::vector<Vector3>& positions, const RenderSettings& settings,
                                       int iterations)
{
    requestProbeBake(positions, settings, iterations);
    while (stepProbeBake(6)) {}
}

}  // namespace CnaRoom
