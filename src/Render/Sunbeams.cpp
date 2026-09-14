// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/Sunbeams.hpp"

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
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <exception>
#include <vector>
#include <string>

namespace CnaRoom {

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
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
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;

namespace {

// The quad's uv is clip-space mapped to 0..1 (v up), the orientation a render
// target is sampled in by a ShaderEffect (R-8: no flip).
constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;
out vec2 vUv;
void main() { gl_Position = vec4(aPosition.xy, 0.0, 1.0); vUv = aTexCoord; }
)";

constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vUv;
uniform sampler2D uDepth;
uniform sampler2D uShadowMap;
uniform int uDepthPacked;
uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform float uFar;
uniform int uCascadeCount;
uniform mat4 uCascadeMatrices[4];
uniform vec4 uCascadeSplits;
uniform vec2 uShadowTexel;
uniform float uShadowBias;
uniform vec3 uLightDirection;
uniform vec3 uLightColour;
uniform float uDensity;
uniform float uAnisotropy;
uniform int uSteps;
uniform vec3 uRoomMin;
uniform vec3 uRoomMax;
uniform int uDebug;
uniform int uLampOn;
uniform vec3 uLampPosition;
uniform vec3 uLampColour;
uniform float uLampRange;
uniform float uLampBias;
uniform float uLampDensity;
uniform float uLampG;
uniform int uLampHasCube;
uniform samplerCube uLampCube;
out vec4 fragColor;

// texelFetch throughout: a custom-effect draw keeps whatever sampler state
// the last stock draw left on each unit (R-33), and a packed depth or a
// shadow tap must not be filtered.
float decodeDepth(vec2 uv) {
    ivec2 size = textureSize(uDepth, 0);
    vec4 p = texelFetch(uDepth, clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0);
    if (uDepthPacked == 1) return dot(p, vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0));
    return p.r;
}

float atlasAt(vec2 uv) {
    ivec2 size = textureSize(uShadowMap, 0);
    return texelFetch(uShadowMap, clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0).r;
}

mat4 cascadeMatrix(int index) {
    mat4 m = uCascadeMatrices[0];
    if (index == 1) m = uCascadeMatrices[1];
    if (index == 2) m = uCascadeMatrices[2];
    if (index == 3) m = uCascadeMatrices[3];
    return m;
}

float cascadeSplit(int index) {
    float s = uCascadeSplits.x;
    if (index == 1) s = uCascadeSplits.y;
    if (index == 2) s = uCascadeSplits.z;
    if (index == 3) s = uCascadeSplits.w;
    return s;
}

// One tap of the atlas, the receivers' convention: the cascade matrix lands
// in the cascade's own slice, z in 0..1, lit where z - bias <= stored.
float litAt(vec3 p, float viewDepth) {
    int index = uCascadeCount - 1;
    for (int i = 0; i < 4; ++i) {
        if (i >= uCascadeCount) break;
        if (viewDepth <= cascadeSplit(i)) { index = i; break; }
    }
    vec4 atlas = cascadeMatrix(index) * vec4(p, 1.0);
    vec3 uv = atlas.xyz / atlas.w;
    float slice = 1.0 / float(uCascadeCount);
    float x0 = float(index) * slice;
    if (uv.x < x0 || uv.x > x0 + slice || uv.y < 0.0 || uv.y > 1.0 || uv.z > 1.0) return 1.0;
    vec2 at = clamp(uv.xy, vec2(x0 + uShadowTexel.x, uShadowTexel.y), vec2(x0 + slice - uShadowTexel.x, 1.0 - uShadowTexel.y));
    float occluder = atlasAt(at);
    return (uv.z - uShadowBias <= occluder) ? 1.0 : 0.0;
}

void main() {
    vec2 ndc = vUv * 2.0 - 1.0;
    float depth01 = decodeDepth(vUv);
    if (uDebug == 1) { fragColor = vec4(vec3(depth01 * uFar / 10.0), 1.0); return; }
    vec4 pNear = uInverseViewProjection * vec4(ndc, -1.0, 1.0);
    vec4 pFar = uInverseViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 near = pNear.xyz / pNear.w;
    vec3 far = pFar.xyz / pFar.w;
    vec3 dir = normalize(far - near);
    float cosF = max(dot(dir, uCameraForward), 1e-3);
    float viewDepth = depth01 * uFar;
    float tEnd = (depth01 >= 0.999 ? uFar * 4.0 : viewDepth) / cosF;
    vec3 surface = uCameraPosition + dir * tEnd;
    if (uDebug == 2) { fragColor = vec4(fract(surface), 1.0); return; }
    if (uDebug == 3) { fragColor = vec4(vec3(litAt(surface, viewDepth)), 1.0); return; }
    if (uDebug == 4) { fragColor = vec4(vec3(atlasAt(vUv)), 1.0); return; }   // the atlas itself (rows bottom-up)
    // The ray clipped to the room's box.
    vec3 safeDir = vec3(abs(dir.x) < 1e-5 ? 1e-5 : dir.x, abs(dir.y) < 1e-5 ? 1e-5 : dir.y, abs(dir.z) < 1e-5 ? 1e-5 : dir.z);
    vec3 invDir = 1.0 / safeDir;
    vec3 t0 = (uRoomMin - uCameraPosition) * invDir;
    vec3 t1 = (uRoomMax - uCameraPosition) * invDir;
    vec3 tMin = min(t0, t1), tMax = max(t0, t1);
    float tIn = max(max(tMin.x, tMin.y), max(tMin.z, 0.0));
    float tOut = min(min(tMax.x, tMax.y), min(tMax.z, tEnd));
    if (tOut <= tIn) { fragColor = vec4(0.0); return; }
    float len = tOut - tIn;
    float stepLen = len / float(uSteps);
    // A per-pixel offset hides the step banding as fine noise.
    float jitter = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233))) * 43758.5453);
    float t = tIn + stepLen * jitter;
    float g = uAnisotropy;
    float cosT = -dot(uLightDirection, dir);
    float phase = (1.0 - g * g) / (4.0 * 3.14159265 * pow(1.0 + g * g - 2.0 * g * cosT, 1.5));
    float scatter = 0.0;
    float lampScatter = 0.0;
    float transmittance = 1.0;
    float gl2 = uLampG * uLampG;
    for (int i = 0; i < 64; ++i) {
        if (i >= uSteps) break;
        vec3 p = uCameraPosition + dir * t;
        float lit = uCascadeCount > 0 ? litAt(p, t * cosF) : 0.0;
        scatter += lit * transmittance * uDensity * stepLen;
        if (uLampOn == 1) {
            // The lamp: inverse-square (the effect's 1 / (1 + d^2)), its cube
            // shadow looked up by the light-to-point direction, the phase
            // against the light's travel from the lamp to this point.
            vec3 toLamp = uLampPosition - p;
            float d = max(length(toLamp), 1e-3);
            vec3 travel = -toLamp / d;
            float vis = 1.0;
            if (uLampHasCube == 1) {
                float here = clamp(d / uLampRange, 0.0, 1.0);
                float occluder = texture(uLampCube, travel).r;
                vis = (here - uLampBias <= occluder) ? 1.0 : 0.0;
            }
            float cosL = dot(travel, dir);
            float lampPhase = (1.0 - gl2) / (4.0 * 3.14159265 * pow(1.0 + gl2 - 2.0 * uLampG * cosL, 1.5));
            lampScatter += vis * lampPhase / (1.0 + d * d) * transmittance * uLampDensity * stepLen;
        }
        transmittance *= exp(-uDensity * stepLen);
        t += stepLen;
    }
    if (uDebug == 5) { fragColor = vec4(vec3(scatter * 2.0 + lampScatter * 200.0), 1.0); return; }
    if (uDebug == 6) { fragColor = vec4(len / 8.0, tIn / 8.0, tOut / 8.0, 1.0); return; }
    fragColor = vec4(uLightColour * (phase * scatter) + uLampColour * lampScatter, 1.0);
}
)";

// The upsample: the half-size march added over the scene, sampled bilinearly
// (whatever sampler the unit carries, R-33, which for the stock draws is
// linear).
constexpr const char* kCopyFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vUv;
uniform sampler2D uBeams;
out vec4 fragColor;
void main() { fragColor = vec4(texture(uBeams, vUv).rgb, 1.0); }
)";

// The motes: one quad per speck, its centre drifting through the room box,
// lit by the same atlas compare as the air (in the vertex shader, one tap per
// speck), collapsed when unlit. Attribute locations follow the vertex
// declaration's element order (Position, Color, TextureCoordinate).
constexpr const char* kMoteVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;   // spawn in the unit cube
layout(location = 1) in vec4 aColor;      // r: drift phase, g: size jitter, b: speed, a: unused
layout(location = 2) in vec2 aTexCoord;   // corner -1..1
uniform sampler2D uShadowMap;
uniform mat4 uViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform int uCascadeCount;
uniform mat4 uCascadeMatrices[4];
uniform vec4 uCascadeSplits;
uniform vec2 uShadowTexel;
uniform float uShadowBias;
uniform vec3 uRoomMin;
uniform vec3 uRoomMax;
uniform float uTime;
uniform vec2 uPixel;      // a mote's half size in clip units: pixels * 2 / viewport
uniform int uCount;
out vec2 vCorner;
out float vLit;

float atlasAt(vec2 uv) {
    ivec2 size = textureSize(uShadowMap, 0);
    return texelFetch(uShadowMap, clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0).r;
}
mat4 cascadeMatrix(int index) {
    mat4 m = uCascadeMatrices[0];
    if (index == 1) m = uCascadeMatrices[1];
    if (index == 2) m = uCascadeMatrices[2];
    if (index == 3) m = uCascadeMatrices[3];
    return m;
}
float cascadeSplit(int index) {
    float s = uCascadeSplits.x;
    if (index == 1) s = uCascadeSplits.y;
    if (index == 2) s = uCascadeSplits.z;
    if (index == 3) s = uCascadeSplits.w;
    return s;
}
float litAt(vec3 p, float viewDepth) {
    int index = uCascadeCount - 1;
    for (int i = 0; i < 4; ++i) {
        if (i >= uCascadeCount) break;
        if (viewDepth <= cascadeSplit(i)) { index = i; break; }
    }
    vec4 atlas = cascadeMatrix(index) * vec4(p, 1.0);
    vec3 uv = atlas.xyz / atlas.w;
    float slice = 1.0 / float(uCascadeCount);
    float x0 = float(index) * slice;
    if (uv.x < x0 || uv.x > x0 + slice || uv.y < 0.0 || uv.y > 1.0 || uv.z > 1.0) return 1.0;
    vec2 at = clamp(uv.xy, vec2(x0 + uShadowTexel.x, uShadowTexel.y), vec2(x0 + slice - uShadowTexel.x, 1.0 - uShadowTexel.y));
    return (uv.z - uShadowBias <= atlasAt(at)) ? 1.0 : 0.0;
}

void main() {
    int mote = gl_VertexID / 4;
    if (mote >= uCount) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vCorner = vec2(0.0); vLit = 0.0; return; }
    float phase = aColor.r * 6.2831853;
    float jitter = 0.6 + 0.8 * aColor.g;
    float speed = 0.4 + 0.6 * aColor.b;
    vec3 size = uRoomMax - uRoomMin;
    // A slow wander: convection lifts and turns the specks, nothing falls.
    vec3 drift = vec3(sin(uTime * 0.11 * speed + phase), 0.5 * sin(uTime * 0.07 * speed + phase * 1.3), cos(uTime * 0.09 * speed + phase * 0.7)) * 0.25
               + vec3(0.02, 0.006, -0.015) * uTime * speed;
    vec3 p = uRoomMin + mod(aPosition * size + drift, size);
    float viewDepth = dot(p - uCameraPosition, uCameraForward);
    float lit = viewDepth > 0.05 ? litAt(p, viewDepth) : 0.0;
    if (lit <= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vCorner = vec2(0.0); vLit = 0.0; return; }
    // A speck is a point of glare to the camera: a fixed few pixels whatever
    // its distance, so the offset is applied in clip space.
    vec4 clip = uViewProjection * vec4(p, 1.0);
    if (clip.w <= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vCorner = vec2(0.0); vLit = 0.0; return; }
    clip.xy += aTexCoord * uPixel * jitter * clip.w;
    gl_Position = clip;
    vCorner = aTexCoord;
    vLit = lit;
}
)";

constexpr const char* kMoteFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vCorner;
in float vLit;
uniform vec3 uRadiance;
out vec4 fragColor;
void main() {
    if (vLit <= 0.0) discard;
    float r = length(vCorner);
    float shape = 1.0 - smoothstep(0.3, 1.0, r);
    if (shape <= 0.002) discard;
    fragColor = vec4(uRadiance * shape, 1.0);
}
)";

std::uint32_t xorshift(std::uint32_t& state)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

float unitRandom(std::uint32_t& state) { return static_cast<float>(xorshift(state) & 0xFFFFFFu) / static_cast<float>(0x1000000u); }

}  // namespace

Sunbeams::Sunbeams(GraphicsDevice& device) : device_(device)
{
    try
    {
        if (!device_.SupportsCapability(CNA::GraphicsCapability::CustomEffects) || !device_.ExecutesShaderEffectSourceEXT())
        {
            reason_ = "the renderer does not execute shader-effect source";
            return;
        }
        effect_ = std::make_unique<ShaderEffect>(device_, kVertexSource, kFragmentSource);
        if (!effect_->IsEffectValid())
        {
            reason_ = "the sunbeam shader did not compile: " + effect_->GetCompileErrorEXT();
            effect_.reset();
            return;
        }
        const std::array<VertexPositionColorTexture, 4> vertices = {
            VertexPositionColorTexture(Vector3(-1.0f, -1.0f, 0.0f), Color::White, Vector2(0.0f, 0.0f)),
            VertexPositionColorTexture(Vector3(1.0f, -1.0f, 0.0f), Color::White, Vector2(1.0f, 0.0f)),
            VertexPositionColorTexture(Vector3(1.0f, 1.0f, 0.0f), Color::White, Vector2(1.0f, 1.0f)),
            VertexPositionColorTexture(Vector3(-1.0f, 1.0f, 0.0f), Color::White, Vector2(0.0f, 1.0f)),
        };
        const std::array<std::uint16_t, 6> indices = {0, 1, 2, 0, 2, 3};
        quad_ = std::make_unique<VertexBuffer>(device_, VertexPositionColorTexture::getVertexDeclarationStatic(), 4, BufferUsage::WriteOnly);
        quad_->SetData(vertices.data(), 4);
        quadIndices_ = std::make_unique<IndexBuffer>(device_, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
        quadIndices_->SetData(indices.data(), 6);
        copyEffect_ = std::make_unique<ShaderEffect>(device_, kVertexSource, kCopyFragmentSource);
        if (!copyEffect_->IsEffectValid())
        {
            CNA::Logger::Warn("living-room-simulator: the sunbeam upsample shader did not compile: " + copyEffect_->GetCompileErrorEXT());
            copyEffect_.reset();
        }
        // The motes' buffers: a fixed population, drawn in part.
        moteEffect_ = std::make_unique<ShaderEffect>(device_, kMoteVertexSource, kMoteFragmentSource);
        if (!moteEffect_->IsEffectValid())
        {
            CNA::Logger::Warn("living-room-simulator: the dust-mote shader did not compile: " + moteEffect_->GetCompileErrorEXT());
            moteEffect_.reset();
        }
        else
        {
            moteCapacity_ = 1200;
            std::vector<VertexPositionColorTexture> moteVertices(static_cast<std::size_t>(moteCapacity_) * 4u);
            std::vector<std::uint32_t> moteIndices(static_cast<std::size_t>(moteCapacity_) * 6u);
            std::uint32_t rng = 0x2545F491u;
            const Vector2 corners[4] = {Vector2(-1.0f, -1.0f), Vector2(1.0f, -1.0f), Vector2(1.0f, 1.0f), Vector2(-1.0f, 1.0f)};
            for (int i = 0; i < moteCapacity_; ++i)
            {
                const Vector3 spawn(unitRandom(rng), unitRandom(rng), unitRandom(rng));
                const float phase = unitRandom(rng), jitter = unitRandom(rng), speed = unitRandom(rng);
                for (int c = 0; c < 4; ++c)
                {
                    VertexPositionColorTexture& v = moteVertices[static_cast<std::size_t>(i) * 4u + static_cast<std::size_t>(c)];
                    v.Position = spawn;
                    v.Color = Color(static_cast<int>(phase * 255.0f), static_cast<int>(jitter * 255.0f), static_cast<int>(speed * 255.0f), 255);
                    v.TextureCoordinate = corners[c];
                }
                const std::uint32_t base = static_cast<std::uint32_t>(i) * 4u;
                const std::uint32_t quad[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
                for (int k = 0; k < 6; ++k) moteIndices[static_cast<std::size_t>(i) * 6u + static_cast<std::size_t>(k)] = quad[k];
            }
            moteVertices_ = std::make_unique<VertexBuffer>(device_, VertexPositionColorTexture::getVertexDeclarationStatic(),
                                                           static_cast<int>(moteVertices.size()), BufferUsage::WriteOnly);
            moteVertices_->SetData(moteVertices.data(), static_cast<int>(moteVertices.size()));
            moteIndices_ = std::make_unique<IndexBuffer>(device_, IndexElementSize::ThirtyTwoBits, static_cast<int>(moteIndices.size()),
                                                         BufferUsage::WriteOnly);
            moteIndices_->SetData(moteIndices.data(), static_cast<int>(moteIndices.size()));
        }
        supported_ = true;
    }
    catch (const std::exception& error)
    {
        reason_ = std::string("sunbeam setup threw: ") + error.what();
        CNA::Logger::Warn("living-room-simulator: " + reason_);
    }
}

Sunbeams::~Sunbeams() = default;

bool Sunbeams::ensureHalfTarget(int width, int height)
{
    const int w = std::max(width / 2, 1), h = std::max(height / 2, 1);
    if (halfTarget_ != nullptr && halfWidth_ == w && halfHeight_ == h) return true;
    if (halfTargetFailed_) return false;
    try
    {
        // HdrBlendable, not HalfVector4: this target is drawn into with additive blending (the
        // beams accumulate), and HiDef permits render-target blending for HdrBlendable but not
        // for HalfVector4, even though CNA backs both with the same storage.
        const SurfaceFormat format = device_.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable) ? SurfaceFormat::HdrBlendable
                                                                                                                  : SurfaceFormat::Color;
        halfTarget_ = std::make_unique<RenderTarget2D>(device_, w, h, false, format, DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        halfWidth_ = w;
        halfHeight_ = h;
        return true;
    }
    catch (const std::exception& error)
    {
        halfTargetFailed_ = true;
        CNA::Logger::Warn(std::string("living-room-simulator: the sunbeam half target failed, marching at full size: ") + error.what());
        return false;
    }
}

void Sunbeams::drawQuad()
{
    device_.SetVertexBuffer(quad_.get());
    device_.setIndicesProperty(quadIndices_.get());
    device_.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
    device_.SetVertexBuffer(nullptr);
    device_.setIndicesProperty(nullptr);
}

void Sunbeams::marchQuad(const Inputs& in, bool opaque)
{
    device_.setBlendStateProperty(opaque ? BlendState::Opaque : BlendState::Additive);
    device_.setDepthStencilStateProperty(DepthStencilState::None);
    device_.setRasterizerStateProperty(RasterizerState::CullNone);
    effect_->Apply();
    effect_->SetUniformInt("uDepth", 0);
    effect_->SetUniformInt("uShadowMap", 1);
    effect_->SetUniformInt("uDepthPacked", in.depthPacked ? 1 : 0);
    effect_->SetUniformMat4("uInverseViewProjection", &in.inverseViewProjection.M11);
    effect_->SetUniformVec3("uCameraPosition", in.cameraPosition.X, in.cameraPosition.Y, in.cameraPosition.Z);
    effect_->SetUniformVec3("uCameraForward", in.cameraForward.X, in.cameraForward.Y, in.cameraForward.Z);
    effect_->SetUniformFloat("uFar", in.prepassFarPlane);
    effect_->SetUniformInt("uCascadeCount", std::min(in.cascadeCount, 4));
    // Per element: the array setter expects column-major data and the single
    // setter takes an XNA matrix as it is (the sky's inverse goes the same way).
    for (int i = 0; i < 4; ++i)
        effect_->SetUniformMat4(("uCascadeMatrices[" + std::to_string(i) + "]").c_str(), &in.cascadeMatrices[static_cast<std::size_t>(i)].M11);
    effect_->SetUniformVec4("uCascadeSplits", in.splitDistances[0], in.splitDistances[1], in.splitDistances[2], in.splitDistances[3]);
    effect_->SetUniformVec2("uShadowTexel", in.shadowTexel.X, in.shadowTexel.Y);
    effect_->SetUniformFloat("uShadowBias", in.shadowBias);
    effect_->SetUniformVec3("uLightDirection", in.lightDirection.X, in.lightDirection.Y, in.lightDirection.Z);
    effect_->SetUniformVec3("uLightColour", in.lightColour.X, in.lightColour.Y, in.lightColour.Z);
    effect_->SetUniformFloat("uDensity", std::max(in.density, 0.0f));
    effect_->SetUniformFloat("uAnisotropy", std::clamp(in.anisotropy, -0.95f, 0.95f));
    effect_->SetUniformInt("uSteps", std::clamp(in.steps, 1, 64));
    effect_->SetUniformVec3("uRoomMin", in.roomMin.X, in.roomMin.Y, in.roomMin.Z);
    effect_->SetUniformVec3("uRoomMax", in.roomMax.X, in.roomMax.Y, in.roomMax.Z);
    effect_->SetUniformInt("uDebug", in.debug);
    effect_->SetUniformInt("uLampOn", in.lampHaze ? 1 : 0);
    effect_->SetUniformVec3("uLampPosition", in.lampPosition.X, in.lampPosition.Y, in.lampPosition.Z);
    effect_->SetUniformVec3("uLampColour", in.lampColour.X, in.lampColour.Y, in.lampColour.Z);
    effect_->SetUniformFloat("uLampRange", std::max(in.lampRange, 0.1f));
    effect_->SetUniformFloat("uLampBias", in.lampBias);
    effect_->SetUniformFloat("uLampDensity", std::max(in.lampDensity, 0.0f));
    effect_->SetUniformFloat("uLampG", std::clamp(in.lampAnisotropy, -0.95f, 0.95f));
    effect_->SetUniformInt("uLampHasCube", in.lampHaze && in.lampCube != nullptr ? 1 : 0);
    effect_->SetUniformInt("uLampCube", 2);
    // Both through the device's slots and the effect's binding: the device
    // applies its slots at the draw, over whatever the effect bound.
    effect_->SetTexture(0, *in.depth);
    if (in.shadowAtlas != nullptr) effect_->SetTexture(1, *in.shadowAtlas);
    if (in.lampHaze && in.lampCube != nullptr) effect_->SetTexture(2, *in.lampCube);
    drawQuad();
}

void Sunbeams::march(const Inputs& in, int width, int height)
{
    // Into the half target, opaque (the upsample adds it). Skipped for the
    // debug views (full size, over the scene) and when the target failed;
    // draw() then marches at full size. Re-binding the pipeline's scene
    // target would discard it, so this runs before the pipeline opens.
    halfMarched_ = false;
    if (!supported_ || in.depth == nullptr || width <= 0 || height <= 0) return;
    if ((in.shadowAtlas == nullptr || in.cascadeCount <= 0) && !in.lampHaze) return;
    if (!in.halfResolution || in.debug != 0 || copyEffect_ == nullptr || !ensureHalfTarget(width, height)) return;
    device_.SetRenderTarget(halfTarget_.get());
    device_.Clear(Color::Black);
    marchQuad(in, true);
    device_.SetRenderTarget(nullptr);
    device_.setBlendStateProperty(BlendState::Opaque);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    halfMarched_ = true;
}

void Sunbeams::draw(const Inputs& in, int width, int height)
{
    if (!supported_ || in.depth == nullptr || width <= 0 || height <= 0) return;
    if ((in.shadowAtlas == nullptr || in.cascadeCount <= 0) && !in.lampHaze) return;
    if (halfMarched_)
    {
        // The half-size march, added over the scene. halfTarget_ is an HDR format
        // (HdrBlendable/HalfVector4); HiDef only permits point sampling of float/half formats
        // (GraphicsProfileDrawStateFormatTest's FloatAndHalfTexturesRequirePurePointFiltering),
        // so the upsample is point rather than the bilinear this comment used to promise.
        halfMarched_ = false;
        device_.setBlendStateProperty(BlendState::Additive);
        device_.setDepthStencilStateProperty(DepthStencilState::None);
        device_.setRasterizerStateProperty(RasterizerState::CullNone);
        device_.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        copyEffect_->Apply();
        copyEffect_->SetUniformInt("uBeams", 0);
        copyEffect_->SetTexture(0, static_cast<Texture2D&>(*halfTarget_));
        drawQuad();
    }
    else
    {
        marchQuad(in, in.debug != 0);
    }
    if (in.debug == 0)
    {
        Inputs sized = in;
        sized.viewportWidth = width;
        sized.viewportHeight = height;
        drawMotes(sized);
    }
    device_.setBlendStateProperty(BlendState::Opaque);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
}

void Sunbeams::drawMotes(const Inputs& in)
{
    if (moteEffect_ == nullptr || in.motes <= 0 || moteCapacity_ <= 0) return;
    const int count = std::min(in.motes, moteCapacity_);
    device_.setBlendStateProperty(BlendState::Additive);
    device_.setDepthStencilStateProperty(DepthStencilState::DepthRead);
    device_.setRasterizerStateProperty(RasterizerState::CullNone);
    moteEffect_->Apply();
    moteEffect_->SetUniformInt("uShadowMap", 0);
    moteEffect_->SetUniformMat4("uViewProjection", &in.viewProjection.M11);
    moteEffect_->SetUniformVec3("uCameraPosition", in.cameraPosition.X, in.cameraPosition.Y, in.cameraPosition.Z);
    moteEffect_->SetUniformVec3("uCameraForward", in.cameraForward.X, in.cameraForward.Y, in.cameraForward.Z);
    moteEffect_->SetUniformVec3("uCameraRight", in.cameraRight.X, in.cameraRight.Y, in.cameraRight.Z);
    moteEffect_->SetUniformVec3("uCameraUp", in.cameraUp.X, in.cameraUp.Y, in.cameraUp.Z);
    moteEffect_->SetUniformInt("uCascadeCount", std::min(in.cascadeCount, 4));
    for (int i = 0; i < 4; ++i)
        moteEffect_->SetUniformMat4(("uCascadeMatrices[" + std::to_string(i) + "]").c_str(), &in.cascadeMatrices[static_cast<std::size_t>(i)].M11);
    moteEffect_->SetUniformVec4("uCascadeSplits", in.splitDistances[0], in.splitDistances[1], in.splitDistances[2], in.splitDistances[3]);
    moteEffect_->SetUniformVec2("uShadowTexel", in.shadowTexel.X, in.shadowTexel.Y);
    moteEffect_->SetUniformFloat("uShadowBias", in.shadowBias);
    moteEffect_->SetUniformVec3("uRoomMin", in.roomMin.X, in.roomMin.Y, in.roomMin.Z);
    moteEffect_->SetUniformVec3("uRoomMax", in.roomMax.X, in.roomMax.Y, in.roomMax.Z);
    moteEffect_->SetUniformFloat("uTime", in.time);
    // moteSize is in pixels at a 540-line frame; scaled with the frame height.
    const float pixels = std::max(in.moteSize, 0.5f) * static_cast<float>(std::max(in.viewportHeight, 1)) / 540.0f;
    moteEffect_->SetUniformVec2("uPixel", pixels * 2.0f / static_cast<float>(std::max(in.viewportWidth, 1)),
                                pixels * 2.0f / static_cast<float>(std::max(in.viewportHeight, 1)));
    moteEffect_->SetUniformInt("uCount", count);
    // A speck glints with the key light; the same forward bias as the air.
    const float g = std::clamp(in.anisotropy, -0.95f, 0.95f);
    const float forward = (1.0f - g * g) / (4.0f * 3.14159265f * std::pow(1.0f + g * g - 2.0f * g * 0.5f, 1.5f));
    const Vector3 radiance = in.lightColour * (in.moteBrightness * forward * 4.0f);
    moteEffect_->SetUniformVec3("uRadiance", radiance.X, radiance.Y, radiance.Z);
    moteEffect_->SetTexture(0, *in.shadowAtlas);
    device_.SetVertexBuffer(moteVertices_.get());
    device_.setIndicesProperty(moteIndices_.get());
    device_.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, count * 4, 0, count * 2);
    device_.SetVertexBuffer(nullptr);
    device_.setIndicesProperty(nullptr);
}

}  // namespace CnaRoom
