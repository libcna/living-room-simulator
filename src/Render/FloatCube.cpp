// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/FloatCube.hpp"

#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaRoom {

namespace {

// A clip-space quad drawn through the vertex-buffer path: the sprite path
// builds its projection from the 2D target it knows about, and a cube face
// is not one (its quad lands off the face). Attribute locations follow the
// VertexPositionColorTexture declaration order (position, colour, uv).
constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPosition.xy, 0.0, 1.0);
    vUv = aTexCoord;
}
)";

// Decodes one face of the RGBE strip into the bound cube face by integer
// texel fetch (no filtering across mantissa/exponent boundaries).
constexpr const char* kDecodeSource = R"(#version 300 es
precision highp float;
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uSource;
uniform int uFace;
uniform int uSize;
uniform int uFlipV;
out vec4 fragColor;
void main() {
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    if (uFlipV == 1) y = uSize - 1 - y;
    vec4 t = texelFetch(uSource, ivec2(uFace * uSize + x, y), 0);
    // Round the exponent code: a sampler's mediump return would otherwise
    // shift every value by a fraction of an octave.
    float e = floor(t.a * 255.0 + 0.5) - 128.0;
    vec3 v = t.a > 0.0 ? t.rgb * exp2(e) : vec3(0.0);
    fragColor = vec4(v, 1.0);
}
)";

// Samples the cube in 16 given directions into a 4 x 4 target (the self-test).
constexpr const char* kProbeSource = R"(#version 300 es
precision highp float;
precision highp samplerCube;
in vec2 vUv;
uniform samplerCube uCube;
uniform vec3 uDirections[16];
out vec4 fragColor;
void main() {
    int x = clamp(int(gl_FragCoord.x), 0, 3);
    int y = clamp(int(gl_FragCoord.y), 0, 3);
    fragColor = vec4(texture(uCube, uDirections[y * 4 + x]).rgb, 1.0);
}
)";

constexpr int kTestSize = 4;

}  // namespace

FloatCubeUploader::FloatCubeUploader(GraphicsDevice& device) : device_(device) {}

FloatCubeUploader::~FloatCubeUploader() = default;

bool FloatCubeUploader::supported()
{
    if (!tested_) selfTest();
    return supported_;
}

void FloatCubeUploader::drawQuad(ShaderEffect& effect)
{
    device_.setBlendStateProperty(BlendState::Opaque);
    device_.setDepthStencilStateProperty(DepthStencilState::None);
    device_.setRasterizerStateProperty(RasterizerState::CullNone);
    effect.Apply();
    device_.SetVertexBuffer(quad_.get());
    device_.setIndicesProperty(quadIndices_.get());
    device_.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
    device_.SetVertexBuffer(nullptr);
    device_.setIndicesProperty(nullptr);
}

void FloatCubeUploader::selfTest()
{
    tested_ = true;
    supported_ = false;
    try
    {
        if (!device_.SupportsCapability(CNA::GraphicsCapability::CustomEffects) || !device_.ExecutesShaderEffectSourceEXT())
        {
            reason_ = "the renderer does not execute shader-effect source";
            CNA::Logger::Warn("cna-room: float cube upload unavailable: " + reason_);
            return;
        }
        if (!device_.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector4))
        {
            reason_ = "HalfVector4 is not a render target format here";
            CNA::Logger::Warn("cna-room: float cube upload unavailable: " + reason_);
            return;
        }
        decode_ = std::make_unique<ShaderEffect>(device_, kVertexSource, kDecodeSource);
        probe_ = std::make_unique<ShaderEffect>(device_, kVertexSource, kProbeSource);
        if (!decode_->IsEffectValid() || !probe_->IsEffectValid())
        {
            reason_ = "float cube shaders did not compile: " + decode_->GetCompileErrorEXT() + probe_->GetCompileErrorEXT();
            CNA::Logger::Warn("cna-room: float cube upload unavailable: " + reason_);
            return;
        }
        const std::array<VertexPositionColorTexture, 4> vertices = {
            VertexPositionColorTexture(Vector3(-1.0f, -1.0f, 0.0f), Color::White, Vector2(0.0f, 1.0f)),
            VertexPositionColorTexture(Vector3(1.0f, -1.0f, 0.0f), Color::White, Vector2(1.0f, 1.0f)),
            VertexPositionColorTexture(Vector3(1.0f, 1.0f, 0.0f), Color::White, Vector2(1.0f, 0.0f)),
            VertexPositionColorTexture(Vector3(-1.0f, 1.0f, 0.0f), Color::White, Vector2(0.0f, 0.0f)),
        };
        const std::array<std::uint16_t, 6> indices = {0, 1, 2, 0, 2, 3};
        quad_ = std::make_unique<VertexBuffer>(device_, VertexPositionColorTexture::getVertexDeclarationStatic(), 4, BufferUsage::WriteOnly);
        quad_->SetData(vertices.data(), 4);
        quadIndices_ = std::make_unique<IndexBuffer>(device_, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
        quadIndices_->SetData(indices.data(), 6);
        readback_ = std::make_unique<RenderTarget2D>(device_, kTestSize, kTestSize, false, SurfaceFormat::HalfVector4,
                                                     DepthFormat::None);

        // A pattern spanning six decades across texels, distinct per face and
        // texel, with the channels within a factor of three of each other (a
        // shared exponent drops a channel a thousand times below the largest;
        // irradiance never is).
        std::vector<Vector3> faces(static_cast<std::size_t>(6 * kTestSize * kTestSize));
        for (int f = 0; f < 6; ++f)
            for (int y = 0; y < kTestSize; ++y)
                for (int x = 0; x < kTestSize; ++x)
                {
                    const float magnitude = std::pow(10.0f, static_cast<float>(x * 4 + y) / 15.0f * 6.0f - 4.0f)
                                            * (1.0f + 0.1f * static_cast<float>(f));
                    faces[static_cast<std::size_t>(f * kTestSize * kTestSize + y * kTestSize + x)] =
                        Vector3(magnitude, magnitude * (0.4f + 0.05f * static_cast<float>(y)), magnitude * (2.5f - 0.1f * static_cast<float>(f)));
                }
        for (const bool flip : {false, true})
        {
            RenderTargetCube cube(device_, kTestSize, false, SurfaceFormat::HalfVector4, DepthFormat::None);
            if (!uploadInto(cube, faces, kTestSize, flip)) continue;
            float worst = 0.0f;
            if (verify(cube, faces, kTestSize, worst))
            {
                flipV_ = flip;
                supported_ = true;
                CNA::Logger::Info("cna-room: half-float cube upload verified (flipV " + std::to_string(flip ? 1 : 0)
                                  + ", worst relative error " + std::to_string(worst) + ")");
                device_.SetRenderTarget(nullptr);
                return;
            }
            reason_ = "readback of a half-float cube did not match the upload (worst relative error " + std::to_string(worst)
                      + ", flipV " + std::to_string(flip ? 1 : 0) + ")";
        }
        device_.SetRenderTarget(nullptr);
    }
    catch (const std::exception& error)
    {
        reason_ = std::string("half-float cube path threw: ") + error.what();
        try { device_.SetRenderTarget(nullptr); } catch (...) {}
    }
    CNA::Logger::Warn("cna-room: float cube upload unavailable: " + reason_);
}

bool FloatCubeUploader::uploadInto(RenderTargetCube& cube, const std::vector<Vector3>& faces, int size, bool flipV)
{
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (faces.size() != 6u * count || size <= 0) return false;
    if (strip_ == nullptr || stripSize_ != size)
    {
        strip_ = std::make_unique<Texture2D>(device_, 6 * size, size);
        stripSize_ = size;
    }
    // RGBE: shared exponent, 8-bit mantissas, ~0.4 % steps over 2^-127..2^127.
    std::vector<Color> texels(6u * count, Color(0, 0, 0, 0));
    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const Vector3& v = faces[static_cast<std::size_t>(f) * count + static_cast<std::size_t>(y) * static_cast<std::size_t>(size)
                                         + static_cast<std::size_t>(x)];
                const float r = std::max(v.X, 0.0f), g = std::max(v.Y, 0.0f), b = std::max(v.Z, 0.0f);
                const float m = std::max(r, std::max(g, b));
                Color c(0, 0, 0, 0);
                if (m > 1e-30f)
                {
                    int e = 0;
                    (void)std::frexp(m, &e);                    // m = mant * 2^e, mant in [0.5, 1)
                    const float scale = std::ldexp(1.0f, -e);   // brings the max into [0.5, 1)
                    const auto q = [&](float value) { return static_cast<int>(std::lround(std::clamp(value * scale, 0.0f, 1.0f) * 255.0f)); };
                    c = Color(q(r), q(g), q(b), std::clamp(e + 128, 1, 255));
                }
                texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(6 * size) + static_cast<std::size_t>(f * size + x)] = c;
            }
    strip_->SetData(texels.data(), static_cast<int>(texels.size()));

    for (int f = 0; f < 6; ++f)
    {
        device_.SetRenderTarget(&cube, static_cast<CubeMapFace>(f));
        device_.Clear(Color::Black);
        device_.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        decode_->Apply();
        decode_->SetUniformInt("uFace", f);
        decode_->SetUniformInt("uSize", size);
        decode_->SetUniformInt("uFlipV", flipV ? 1 : 0);
        decode_->SetUniformInt("uSource", 0);
        decode_->SetTexture(0, *strip_);
        drawQuad(*decode_);
    }
    device_.SetRenderTarget(nullptr);
    return true;
}

void FloatCubeUploader::sampleFace(TextureCube& cube, int face, std::vector<PackedVector::HalfVector4>& pixels)
{
    float directions[16 * 3];
    for (int y = 0; y < kTestSize; ++y)
        for (int x = 0; x < kTestSize; ++x)
        {
            const Vector3 d = CNA::Graphics::EnvironmentProcessor::faceDirection(
                face, (static_cast<float>(x) + 0.5f) / kTestSize, (static_cast<float>(y) + 0.5f) / kTestSize);
            directions[(y * kTestSize + x) * 3] = d.X;
            directions[(y * kTestSize + x) * 3 + 1] = d.Y;
            directions[(y * kTestSize + x) * 3 + 2] = d.Z;
        }
    device_.SetRenderTarget(readback_.get());
    device_.Clear(Color::Black);
    device_.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
    probe_->Apply();
    probe_->SetUniformVec3Array("uDirections", directions, 16);
    probe_->SetUniformInt("uCube", 1);
    probe_->SetTexture(1, cube);
    drawQuad(*probe_);
    device_.SetRenderTarget(nullptr);
    pixels.resize(static_cast<std::size_t>(kTestSize * kTestSize));
    readback_->GetData(pixels.data(), static_cast<int>(pixels.size()));
}

bool FloatCubeUploader::verify(RenderTargetCube& cube, const std::vector<Vector3>& faces, int size, float& worstError)
{
    // Only the 4 x 4 test layout: 16 directions per face at the texel centres.
    if (size != kTestSize) return false;
    worstError = 0.0f;
    std::vector<PackedVector::HalfVector4> pixels;
    for (int f = 0; f < 6; ++f)
    {
        sampleFace(cube, f, pixels);
        if (f == 0)
        {
            std::string seen;
            for (int i = 0; i < 4; ++i)
            {
                const Vector4 v = pixels[static_cast<std::size_t>(i)].ToVector4();
                seen += "(" + std::to_string(v.X) + "," + std::to_string(v.Y) + "," + std::to_string(v.Z) + ") ";
            }
            CNA::Logger::Info("cna-room: float cube self-test +X sampled row 0: " + seen + "expected ("
                              + std::to_string(faces[0].X) + "," + std::to_string(faces[0].Y) + "," + std::to_string(faces[0].Z) + ") ...");
        }
        for (int y = 0; y < kTestSize; ++y)
            for (int x = 0; x < kTestSize; ++x)
            {
                // The 2D readback may itself be flipped (R-8): accept either row
                // order by comparing against both candidate rows and keeping the better.
                const Vector4 got = pixels[static_cast<std::size_t>(y * kTestSize + x)].ToVector4();
                float best = 1e9f;
                for (const int row : {y, kTestSize - 1 - y})
                {
                    const Vector3& want = faces[static_cast<std::size_t>(f * kTestSize * kTestSize + row * kTestSize + x)];
                    const float err = std::max({std::abs(got.X - want.X) / std::max(want.X, 1e-4f),
                                                std::abs(got.Y - want.Y) / std::max(want.Y, 1e-4f),
                                                std::abs(got.Z - want.Z) / std::max(want.Z, 1e-4f)});
                    best = std::min(best, err);
                }
                worstError = std::max(worstError, best);
            }
    }
    return worstError < 0.02f;
}

std::unique_ptr<RenderTargetCube> FloatCubeUploader::upload(const std::vector<Vector3>& faces, int size)
{
    if (!supported()) return nullptr;
    try
    {
        auto cube = std::make_unique<RenderTargetCube>(device_, size, false, SurfaceFormat::HalfVector4, DepthFormat::None, 0,
                                                       RenderTargetUsage::PreserveContents);
        if (!uploadInto(*cube, faces, size, flipV_)) return nullptr;
        return cube;
    }
    catch (const std::exception& error)
    {
        CNA::Logger::Warn(std::string("cna-room: float cube upload failed: ") + error.what());
        try { device_.SetRenderTarget(nullptr); } catch (...) {}
        return nullptr;
    }
}

}  // namespace CnaRoom
