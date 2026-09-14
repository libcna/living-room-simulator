// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/TelevisionContent.hpp"

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <cmath>
#include <cstddef>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaRoom {

namespace {

constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
uniform float uTime;
uniform float uFlipV;
out vec4 fragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { s += a * noise(p); p *= 2.03; a *= 0.5; }
    return s;
}

// Programme 1: a landscape at sunset, panning slowly.
vec3 landscape(vec2 uv, float t) {
    float pan = t * 0.02;
    vec2 p = vec2(uv.x + pan, uv.y);
    vec3 sky = mix(vec3(0.95, 0.55, 0.25), vec3(0.25, 0.35, 0.65), smoothstep(0.35, 1.0, uv.y));
    float sunD = length((uv - vec2(0.62 - pan * 0.3, 0.42)) * vec2(1.0, 1.78));
    sky += vec3(1.0, 0.85, 0.5) * smoothstep(0.09, 0.03, sunD);
    float cloud = fbm(vec2(p.x * 3.0 + t * 0.05, uv.y * 6.0));
    sky = mix(sky, vec3(0.95, 0.8, 0.75), smoothstep(0.55, 0.8, cloud) * smoothstep(0.4, 0.6, uv.y) * 0.8);
    float hills = 0.30 + 0.08 * sin(p.x * 6.0) + 0.05 * sin(p.x * 17.0 + 1.0) + 0.03 * fbm(vec2(p.x * 20.0, 0.0));
    float far = 0.40 + 0.06 * sin(p.x * 3.0 + 2.0) + 0.04 * fbm(vec2(p.x * 9.0, 3.0));
    vec3 c = sky;
    c = mix(c, vec3(0.35, 0.3, 0.45), step(uv.y, far));
    c = mix(c, vec3(0.12, 0.2, 0.1), step(uv.y, hills));
    float water = step(uv.y, 0.18);
    vec3 sea = sky * 0.6 + vec3(0.02, 0.05, 0.1) + 0.05 * sin(uv.y * 200.0 + t * 3.0 + uv.x * 30.0);
    c = mix(c, sea, water);
    return c;
}

// Programme 2: a studio set: coloured panels, a rising chart, a ticker.
vec3 studio(vec2 uv, float t) {
    vec3 c = mix(vec3(0.08, 0.1, 0.2), vec3(0.15, 0.2, 0.35), uv.y);
    vec2 g = floor(uv * vec2(6.0, 3.0));
    float k = hash(g + floor(t * 0.5));
    vec3 panel = vec3(0.2 + 0.6 * k, 0.3 + 0.5 * hash(g + 3.0), 0.5 + 0.5 * hash(g + 7.0));
    vec2 f = fract(uv * vec2(6.0, 3.0));
    float inset = step(0.08, f.x) * step(f.x, 0.92) * step(0.1, f.y) * step(f.y, 0.9);
    c = mix(c, panel * 0.5, inset * step(0.45, uv.y));
    // Bar chart in the lower half.
    float bar = floor(uv.x * 10.0);
    float h = 0.05 + 0.3 * (0.5 + 0.5 * sin(bar * 1.7 + t * 0.8));
    float inBar = step(0.05, fract(uv.x * 10.0)) * step(fract(uv.x * 10.0), 0.95) * step(uv.y, h) * step(0.1, uv.y);
    c = mix(c, vec3(0.9, 0.6, 0.2), inBar);
    // Ticker band.
    float band = step(uv.y, 0.09);
    float text = step(0.5, noise(vec2(uv.x * 60.0 + t * 8.0, uv.y * 40.0))) * step(0.02, uv.y) * step(uv.y, 0.07);
    c = mix(c, vec3(0.75, 0.1, 0.1), band);
    c = mix(c, vec3(1.0), text * band);
    return c;
}

// Programme 3: colour bars.
vec3 testCard(vec2 uv) {
    float i = floor(uv.x * 8.0);
    vec3 bars[8];
    bars[0] = vec3(1.0); bars[1] = vec3(1.0, 1.0, 0.0); bars[2] = vec3(0.0, 1.0, 1.0); bars[3] = vec3(0.0, 1.0, 0.0);
    bars[4] = vec3(1.0, 0.0, 1.0); bars[5] = vec3(1.0, 0.0, 0.0); bars[6] = vec3(0.0, 0.0, 1.0); bars[7] = vec3(0.1);
    vec3 c = bars[int(clamp(i, 0.0, 7.0))];
    if (uv.y < 0.25) c = vec3(uv.x);   // grey ramp
    return c * 0.75;
}

void main() {
    vec2 uv = vec2(TexCoord.x, mix(TexCoord.y, 1.0 - TexCoord.y, uFlipV));
    uv.y = 1.0 - uv.y;    // image origin at the top
    float t = uTime;
    // Schedule: 40 s landscape, 25 s studio, 5 s test card, with 0.4 s cuts.
    float cycle = mod(t, 70.0);
    vec3 c;
    if (cycle < 40.0) c = landscape(uv, t);
    else if (cycle < 65.0) c = studio(uv, t);
    else c = testCard(uv);
    // Letterbox and a little vignette + flicker.
    float box = step(0.04, uv.y) * step(uv.y, 0.96);
    float vignette = 1.0 - 0.35 * pow(length((uv - 0.5) * vec2(1.0, 1.4)), 2.0);
    float flicker = 0.97 + 0.03 * sin(t * 47.0 + uv.y * 300.0);
    c = c * box * vignette * flicker;
    // sRGB-encode: the emissive slot is declared sRGB.
    vec3 s = mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
    fragColor = vec4(clamp(s, 0.0, 1.0), 1.0);
}
)";

}  // namespace

TelevisionContent::TelevisionContent(GraphicsDevice& device, int width, int height)
    : device_(device), width_(width), height_(height)
{
    if (!device_.SupportsCapability(CNA::GraphicsCapability::CustomEffects) || !device_.ExecutesShaderEffectSourceEXT())
    {
        reason_ = "the renderer does not execute shader-effect source";
        return;
    }
    effect_ = std::make_unique<ShaderEffect>(device_, kVertexSource, kFragmentSource);
    if (!effect_->IsEffectValid())
    {
        reason_ = "the television shader did not compile: " + effect_->GetCompileErrorEXT();
        CNA::Logger::Error("cna-room: " + reason_);
        effect_.reset();
        return;
    }
    target_ = std::make_unique<RenderTarget2D>(device_, width_, height_, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                               RenderTargetUsage::PreserveContents);
    pass_ = std::make_unique<CNA::Graphics::FullscreenPass>(device_);
    white_ = std::make_unique<Texture2D>(device_, 1, 1);
    const Color pixel = Color::White;
    white_->SetData(&pixel, 1);
    supported_ = true;
}

TelevisionContent::~TelevisionContent() = default;

Texture2D* TelevisionContent::texture() const { return target_.get(); }

void TelevisionContent::update(float seconds)
{
    if (!supported_) return;
    effect_->SetUniformFloat("uTime", seconds);
    effect_->SetUniformFloat("uFlipV", 1.0f);   // FullscreenPass sprite origin (CNA_FINDINGS R-8)
    pass_->draw(white_.get(), target_.get(), effect_.get(), width_, height_);
    readMean();
    device_.SetRenderTarget(nullptr);
}

namespace {
constexpr int kMeanWidth = 32, kMeanHeight = 18;
}

void TelevisionContent::readMean()
{
    // The same programme at 32x18 (smooth at that scale, so its mean is the
    // picture's), read back and decoded from the sRGB it is written in. A
    // readback each frame: 2 KB, and the exposure meter reads the frame anyway.
    if (meanFailed_) return;
    try
    {
        if (meanTarget_ == nullptr)
            meanTarget_ = std::make_unique<RenderTarget2D>(device_, kMeanWidth, kMeanHeight, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                                           RenderTargetUsage::PreserveContents);
        pass_->draw(white_.get(), meanTarget_.get(), effect_.get(), kMeanWidth, kMeanHeight);
        meanPixels_.resize(static_cast<std::size_t>(kMeanWidth) * static_cast<std::size_t>(kMeanHeight));
        meanTarget_->GetData(meanPixels_.data(), static_cast<int>(meanPixels_.size()));
        static const std::array<float, 256> decode = [] {
            std::array<float, 256> table{};
            for (int i = 0; i < 256; ++i)
            {
                const float c = static_cast<float>(i) / 255.0f;
                table[static_cast<std::size_t>(i)] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            }
            return table;
        }();
        Vector3 sum;
        for (const Color& c : meanPixels_)
            sum += Vector3(decode[c.getRProperty()], decode[c.getGProperty()], decode[c.getBProperty()]);
        mean_ = sum / static_cast<float>(meanPixels_.size());
        meanValid_ = true;
    }
    catch (const std::exception& error)
    {
        meanFailed_ = true;
        meanValid_ = false;
        CNA::Logger::Warn(std::string("cna-room: the television's mean readback failed, its glow stays fixed: ") + error.what());
    }
}

}  // namespace CnaRoom
