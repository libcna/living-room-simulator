// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/Precipitation.hpp"

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

#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaRoom {

namespace {

// Attribute locations follow the vertex declaration's element order
// (Position, Color, TextureCoordinate for VertexPositionColorTexture), the
// convention CNA's own prepass shader relies on.
constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;   // spawn position, unit cube 0..1 of the volume
layout(location = 1) in vec4 aColor;      // r: phase, g: size, b: sway phase, a: corner id
layout(location = 2) in vec2 aTexCoord;   // corner -1..1
uniform mat4 uViewProjection;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uVolumeMin;
uniform vec3 uVolumeSize;
uniform vec3 uFall;          // world velocity of a drop (m/s), including wind
uniform float uTime;
uniform float uKind;         // 1 rain, 2 snow, 3 hail
uniform float uIntensity;
uniform float uLength;       // streak length in metres
uniform float uWidth;        // sprite width in metres
out vec2 vCorner;
out float vAlpha;
void main() {
    float phase = aColor.r;
    float sizeJitter = 0.6 + 0.8 * aColor.g;
    float sway = aColor.b * 6.2831853;
    // Cull by intensity: the particle's phase doubles as its "rank".
    if (phase > uIntensity) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vCorner = vec2(0.0); vAlpha = 0.0; return; }
    vec3 base = uVolumeMin + aPosition * uVolumeSize;
    float height = uVolumeSize.y;
    float fallSpeed = length(uFall);
    float t = uTime * fallSpeed + phase * 977.0;
    float dropped = mod(t, height);
    vec3 dir = uFall / max(fallSpeed, 1e-3);
    // Position along the fall direction, wrapped in y; x/z drift wraps too.
    vec3 p = base + dir * dropped;
    p.x = uVolumeMin.x + mod(p.x - uVolumeMin.x, uVolumeSize.x);
    p.z = uVolumeMin.z + mod(p.z - uVolumeMin.z, uVolumeSize.z);
    p.y = uVolumeMin.y + height - mod(uVolumeMin.y + height - p.y, height);
    if (uKind > 1.5 && uKind < 2.5) {
        // Snow wanders sideways as it falls.
        p.x += 0.35 * sin(uTime * 1.3 + sway) * sizeJitter;
        p.z += 0.35 * cos(uTime * 1.1 + sway * 1.7) * sizeJitter;
    }
    vec2 corner = aTexCoord;
    vec3 right = normalize(cross(dir, uCameraUp));
    if (length(cross(dir, uCameraUp)) < 0.05) right = uCameraRight;
    vec3 along = dir;
    float halfWidth = uWidth * sizeJitter * 0.5;
    float halfLength = uLength * sizeJitter * 0.5;
    vec3 world = p + right * (corner.x * halfWidth) + along * (corner.y * halfLength);
    gl_Position = uViewProjection * vec4(world, 1.0);
    vCorner = corner;
    vAlpha = 1.0;
}
)";

constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vCorner;
in float vAlpha;
uniform vec3 uRadiance;
uniform float uKind;
out vec4 fragColor;
void main() {
    if (vAlpha <= 0.0) discard;
    float r2 = dot(vCorner, vCorner);
    float shape;
    if (uKind < 1.5) {
        // Rain: a soft streak, fading towards both ends.
        shape = (1.0 - smoothstep(0.35, 1.0, abs(vCorner.x))) * (1.0 - smoothstep(0.55, 1.0, abs(vCorner.y)));
        shape *= 0.55;
    } else if (uKind < 2.5) {
        shape = 1.0 - smoothstep(0.55, 1.0, sqrt(r2));   // a fluffy flake
    } else {
        shape = 1.0 - smoothstep(0.7, 1.0, sqrt(r2));    // a hard hailstone
        shape *= 1.2;
    }
    if (shape <= 0.002) discard;
    // Premultiplied: adds the lit drop over the background, occludes a little.
    fragColor = vec4(uRadiance * shape, shape * 0.6);
}
)";

std::uint32_t hashRandom(std::uint32_t& state)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

float unit(std::uint32_t& state) { return static_cast<float>(hashRandom(state) & 0xFFFFFFu) / static_cast<float>(0x1000000u); }

}  // namespace

Precipitation::Precipitation(GraphicsDevice& device, int particles) : device_(device), particles_(particles)
{
    if (!device_.SupportsCapability(CNA::GraphicsCapability::CustomEffects) || !device_.ExecutesShaderEffectSourceEXT())
    {
        reason_ = "the renderer does not execute shader-effect source";
        return;
    }
    effect_ = std::make_unique<ShaderEffect>(device_, kVertexSource, kFragmentSource);
    if (!effect_->IsEffectValid())
    {
        reason_ = "the precipitation shader did not compile: " + effect_->GetCompileErrorEXT();
        CNA::Logger::Error("living-room-simulator: " + reason_);
        effect_.reset();
        return;
    }
    std::vector<VertexPositionColorTexture> vertices(static_cast<std::size_t>(particles_) * 4u);
    std::vector<std::uint32_t> indices(static_cast<std::size_t>(particles_) * 6u);
    std::uint32_t rng = 0x9E3779B9u;
    for (int i = 0; i < particles_; ++i)
    {
        const Vector3 spawn(unit(rng), unit(rng), unit(rng));
        const float phase = unit(rng), size = unit(rng), sway = unit(rng);
        const Vector2 corners[4] = {Vector2(-1.0f, -1.0f), Vector2(1.0f, -1.0f), Vector2(1.0f, 1.0f), Vector2(-1.0f, 1.0f)};
        for (int c = 0; c < 4; ++c)
        {
            VertexPositionColorTexture& v = vertices[static_cast<std::size_t>(i) * 4u + static_cast<std::size_t>(c)];
            v.Position = spawn;
            v.Color = Color(static_cast<int>(phase * 255.0f), static_cast<int>(size * 255.0f), static_cast<int>(sway * 255.0f),
                            c * 60);
            v.TextureCoordinate = corners[c];
        }
        const std::uint32_t base = static_cast<std::uint32_t>(i) * 4u;
        const std::uint32_t quad[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
        for (int k = 0; k < 6; ++k) indices[static_cast<std::size_t>(i) * 6u + static_cast<std::size_t>(k)] = quad[k];
    }
    vertices_ = std::make_unique<VertexBuffer>(device_, VertexPositionColorTexture::getVertexDeclarationStatic(),
                                               static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
    vertices_->SetData(vertices.data(), static_cast<int>(vertices.size()));
    indices_ = std::make_unique<IndexBuffer>(device_, IndexElementSize::ThirtyTwoBits, static_cast<int>(indices.size()),
                                             BufferUsage::WriteOnly);
    indices_->SetData(indices.data(), static_cast<int>(indices.size()));
    supported_ = true;
}

Precipitation::~Precipitation() = default;

void Precipitation::draw(const Params& params, const Matrix& view, const Matrix& projection, const Vector3& cameraPosition)
{
    (void)cameraPosition;
    if (!supported_ || params.kind == Kind::None || params.intensity <= 0.001f) return;
    const Matrix viewProjection = view * projection;
    float vp[16];
    viewProjection.ToColumnMajor(vp);
    effect_->SetUniformMat4("uViewProjection", vp);
    // Camera basis from the view matrix rows.
    effect_->SetUniformVec3("uCameraRight", view.M11, view.M21, view.M31);
    effect_->SetUniformVec3("uCameraUp", view.M12, view.M22, view.M32);
    const Vector3 size = params.volume.Max - params.volume.Min;
    effect_->SetUniformVec3("uVolumeMin", params.volume.Min.X, params.volume.Min.Y, params.volume.Min.Z);
    effect_->SetUniformVec3("uVolumeSize", std::max(size.X, 0.1f), std::max(size.Y, 0.1f), std::max(size.Z, 0.1f));
    float fallSpeed = 9.0f, length = 0.22f, width = 0.010f, kind = 1.0f;
    Vector3 radiance = params.radiance;
    switch (params.kind)
    {
        case Kind::Rain: fallSpeed = 9.0f; length = 0.28f; width = 0.010f; kind = 1.0f; radiance = params.radiance * 0.9f; break;
        case Kind::Snow: fallSpeed = 1.1f; length = 0.045f; width = 0.045f; kind = 2.0f; radiance = params.radiance * 2.2f; break;
        case Kind::Hail: fallSpeed = 14.0f; length = 0.018f; width = 0.014f; kind = 3.0f; radiance = params.radiance * 2.0f; break;
        default: break;
    }
    const Vector3 fall(params.wind.X * (params.kind == Kind::Snow ? 0.6f : 0.35f), -fallSpeed,
                       params.wind.Z * (params.kind == Kind::Snow ? 0.6f : 0.35f));
    effect_->SetUniformVec3("uFall", fall.X, fall.Y, fall.Z);
    effect_->SetUniformFloat("uTime", params.timeSeconds);
    effect_->SetUniformFloat("uKind", kind);
    effect_->SetUniformFloat("uIntensity", std::min(1.0f, params.intensity));
    effect_->SetUniformFloat("uLength", length);
    effect_->SetUniformFloat("uWidth", width);
    effect_->SetUniformVec3("uRadiance", radiance.X, radiance.Y, radiance.Z);

    device_.setRasterizerStateProperty(RasterizerState::CullNone);
    device_.setDepthStencilStateProperty(DepthStencilState::DepthRead);
    device_.setBlendStateProperty(BlendState::AlphaBlend);   // premultiplied
    effect_->Apply();
    device_.SetVertexBuffer(vertices_.get());
    device_.setIndicesProperty(indices_.get());
    device_.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, particles_ * 4, 0, particles_ * 2);
    device_.setBlendStateProperty(BlendState::Opaque);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
}

}  // namespace CnaRoom
