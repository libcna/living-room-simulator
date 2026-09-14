// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/Steam.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>

namespace CnaRoom {

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;

namespace {

// Attribute locations follow the vertex declaration's element order
// (Position, Color, TextureCoordinate).
constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;   // x, z: birth offset in the unit disc; y: phase 0..1
layout(location = 1) in vec4 aColor;      // r: size jitter, g: sway phase, b: life jitter, a: noise seed
layout(location = 2) in vec2 aTexCoord;   // corner -1..1
uniform mat4 uViewProjection;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uOrigin;
uniform float uRadius;
uniform float uTime;
uniform float uStrength;
uniform int uCount;
out vec2 vCorner;
out float vAlpha;
out float vSeed;

void main() {
    int puff = gl_VertexID / 4;
    if (puff >= uCount) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vCorner = vec2(0.0); vAlpha = 0.0; vSeed = 0.0; return; }
    float life = 2.4 * (0.8 + 0.4 * aColor.b);
    float age = fract(uTime / life + aPosition.y);
    // Born on the rim, rising a little slower as it thins, swaying in the room's air.
    float rise = 0.22 * (1.0 - pow(1.0 - age, 1.6));
    float swayPhase = aColor.g * 6.2831853;
    vec2 sway = vec2(sin(uTime * 1.7 + swayPhase), cos(uTime * 1.3 + swayPhase * 0.7)) * 0.014 * age
              + vec2(0.012, 0.006) * age * age;   // the convection's lean
    vec3 p = uOrigin + vec3(aPosition.x * uRadius * 0.7 + sway.x, rise, aPosition.z * uRadius * 0.7 + sway.y);
    float size = mix(0.016, 0.080, age) * (0.8 + 0.4 * aColor.r);
    p += (uCameraRight * aTexCoord.x + uCameraUp * aTexCoord.y) * size;
    gl_Position = uViewProjection * vec4(p, 1.0);
    vCorner = aTexCoord;
    vSeed = aColor.a;
    vAlpha = uStrength * smoothstep(0.0, 0.10, age) * pow(1.0 - age, 1.7) * 0.8;
}
)";

constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vCorner;
in float vAlpha;
in float vSeed;
uniform vec3 uRadiance;
uniform float uTime;
out vec4 fragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; ++i) { s += a * noise(p); p = p * 2.1 + vec2(1.7, 9.2); a *= 0.5; }
    return s;
}

void main() {
    if (vAlpha <= 0.0) discard;
    float r = length(vCorner);
    float disc = 1.0 - smoothstep(0.30, 1.0, r);
    // Wisps: a noise that drifts up through the puff, cut at its mid-tones.
    float n = fbm(vCorner * 1.8 + vec2(vSeed * 37.0, vSeed * 11.0 - uTime * 0.45));
    float wisp = smoothstep(0.28, 0.62, n);
    float a = disc * wisp * vAlpha;
    if (a <= 0.003) discard;
    fragColor = vec4(uRadiance * a, a);   // premultiplied
}
)";

float unitRandom(std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

}  // namespace

Steam::Steam(GraphicsDevice& device, int puffs) : device_(device)
{
    if (!device_.SupportsCapability(CNA::GraphicsCapability::CustomEffects) || !device_.ExecutesShaderEffectSourceEXT())
    {
        reason_ = "the renderer does not execute shader-effect source";
        return;
    }
    try
    {
        effect_ = std::make_unique<ShaderEffect>(device_, kVertexSource, kFragmentSource);
        if (!effect_->IsEffectValid())
        {
            reason_ = "the steam shader did not compile: " + effect_->GetCompileErrorEXT();
            CNA::Logger::Warn("cna-room: " + reason_);
            effect_.reset();
            return;
        }
        puffs_ = std::max(puffs, 1);
        std::vector<VertexPositionColorTexture> vertices(static_cast<std::size_t>(puffs_) * 4u);
        std::vector<std::uint32_t> indices(static_cast<std::size_t>(puffs_) * 6u);
        std::uint32_t rng = 0x9E3779B9u;
        const Vector2 corners[4] = {Vector2(-1.0f, -1.0f), Vector2(1.0f, -1.0f), Vector2(1.0f, 1.0f), Vector2(-1.0f, 1.0f)};
        for (int i = 0; i < puffs_; ++i)
        {
            // A birth point in the unit disc (rejection), an even phase.
            float x = 0.0f, z = 0.0f;
            do { x = unitRandom(rng) * 2.0f - 1.0f; z = unitRandom(rng) * 2.0f - 1.0f; } while (x * x + z * z > 1.0f);
            const float phase = (static_cast<float>(i) + unitRandom(rng) * 0.5f) / static_cast<float>(puffs_);
            const Vector3 spawn(x, phase, z);
            const int sizeJitter = static_cast<int>(unitRandom(rng) * 255.0f), swayPhase = static_cast<int>(unitRandom(rng) * 255.0f);
            const int lifeJitter = static_cast<int>(unitRandom(rng) * 255.0f), seed = static_cast<int>(unitRandom(rng) * 255.0f);
            for (int c = 0; c < 4; ++c)
            {
                VertexPositionColorTexture& v = vertices[static_cast<std::size_t>(i) * 4u + static_cast<std::size_t>(c)];
                v.Position = spawn;
                v.Color = Color(sizeJitter, swayPhase, lifeJitter, seed);
                v.TextureCoordinate = corners[c];
            }
            const std::uint32_t base = static_cast<std::uint32_t>(i) * 4u;
            const std::uint32_t quad[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
            for (int k = 0; k < 6; ++k) indices[static_cast<std::size_t>(i) * 6u + static_cast<std::size_t>(k)] = quad[k];
        }
        vertices_ = std::make_unique<VertexBuffer>(device_, VertexPositionColorTexture::getVertexDeclarationStatic(),
                                                   static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
        vertices_->SetData(vertices.data(), static_cast<int>(vertices.size()));
        indices_ = std::make_unique<IndexBuffer>(device_, IndexElementSize::ThirtyTwoBits, static_cast<int>(indices.size()), BufferUsage::WriteOnly);
        indices_->SetData(indices.data(), static_cast<int>(indices.size()));
        supported_ = true;
    }
    catch (const std::exception& error)
    {
        reason_ = std::string("steam setup threw: ") + error.what();
        CNA::Logger::Warn("cna-room: " + reason_);
        effect_.reset();
    }
}

Steam::~Steam() = default;

void Steam::draw(const Params& params)
{
    if (!supported_ || params.strength <= 0.0f) return;
    device_.setBlendStateProperty(BlendState::AlphaBlend);   // premultiplied: one, inverse source alpha
    device_.setDepthStencilStateProperty(DepthStencilState::DepthRead);
    device_.setRasterizerStateProperty(RasterizerState::CullNone);
    effect_->Apply();
    effect_->SetUniformMat4("uViewProjection", &params.viewProjection.M11);
    effect_->SetUniformVec3("uCameraRight", params.cameraRight.X, params.cameraRight.Y, params.cameraRight.Z);
    effect_->SetUniformVec3("uCameraUp", params.cameraUp.X, params.cameraUp.Y, params.cameraUp.Z);
    effect_->SetUniformVec3("uOrigin", params.origin.X, params.origin.Y, params.origin.Z);
    effect_->SetUniformFloat("uRadius", std::max(params.radius, 0.005f));
    effect_->SetUniformFloat("uTime", params.time);
    effect_->SetUniformFloat("uStrength", std::clamp(params.strength, 0.0f, 1.0f));
    effect_->SetUniformInt("uCount", puffs_);
    effect_->SetUniformVec3("uRadiance", params.radiance.X, params.radiance.Y, params.radiance.Z);
    device_.SetVertexBuffer(vertices_.get());
    device_.setIndicesProperty(indices_.get());
    device_.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, puffs_ * 4, 0, puffs_ * 2);
    device_.SetVertexBuffer(nullptr);
    device_.setIndicesProperty(nullptr);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    device_.setBlendStateProperty(BlendState::Opaque);
}

}  // namespace CnaRoom
