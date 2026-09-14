// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/PlanarReflection.hpp"

#include "CnaRoom/Render/Camera.hpp"
#include "CnaRoom/Render/GpuMesh.hpp"
#include "CnaRoom/Render/Material.hpp"
#include "CnaRoom/Render/ReflectionMath.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaRoom {

namespace {

// Attribute locations follow VertexPositionNormalTangentTexture's declaration.
constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aTexCoord;
uniform mat4 uWorld;
uniform mat4 uViewProjection;
uniform vec3 uCameraPosition;
uniform float uNudge;   // metres toward the camera (an overlay over a surface drawn by another shader must win its depth test)
out vec3 vWorld;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUv;
void main() {
    vec4 world = uWorld * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uWorld) * aNormal;
    vTangent = vec4(mat3(uWorld) * aTangent.xyz, aTangent.w);
    vUv = aTexCoord;
    vec3 toCamera = uCameraPosition - world.xyz;
    vec4 placed = vec4(world.xyz + normalize(toCamera) * min(uNudge, length(toCamera) * 0.5), 1.0);
    gl_Position = uViewProjection * placed;
}
)";

constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
precision highp sampler2D;
in vec3 vWorld;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUv;
uniform sampler2D uReflection;
uniform sampler2D uEmissive;
uniform sampler2D uNormalMap;
uniform int uHasNormal;
uniform float uNormalScale;
uniform mat4 uReflectionViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uTint;
uniform vec3 uEmissiveFactor;
uniform vec2 uUvScale;
uniform float uF0;
uniform int uFresnel;
uniform int uHasEmissive;
uniform int uEncodeSrgb;
uniform int uFlipV;
uniform int uEmissiveFlipV;
uniform vec2 uPuddles;   // strength, metres per cell
out vec4 fragColor;
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), u.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), u.x), u.y);
}
vec3 decodeSrgb(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }
vec3 encodeSrgb(vec3 c) { return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }
void main() {
    vec4 r = uReflectionViewProjection * vec4(vWorld, 1.0);
    vec2 uv = r.xy / max(r.w, 1e-4) * 0.5 + 0.5;
    if (uFlipV == 1) uv.y = 1.0 - uv.y;
    vec3 n = normalize(vNormal);
    // Puddles: a wet street's water gathers in low spots (a two-octave value
    // noise over the ground), which mirror; the film between them reflects
    // a quarter as much and is broken up by the surface's normal map.
    float puddle = 1.0;
    if (uPuddles.x > 0.0) {
        vec2 q = vWorld.xz / max(uPuddles.y, 0.05);
        float pn = valueNoise(q) * 0.65 + valueNoise(q * 2.3 + 7.1) * 0.35;
        puddle = smoothstep(0.52, 0.66, pn);
    }
    float share = mix(1.0, mix(0.25, 1.0, puddle), clamp(uPuddles.x, 0.0, 1.0));
    if (uHasNormal == 1) {
        // Droplets on a pane: the tangent-space map bends the reflection
        // lookup and the Fresnel normal (a wet window's broken reflection).
        vec3 tn = texture(uNormalMap, vUv * uUvScale).xyz * 2.0 - 1.0;
        tn.xy *= uNormalScale * mix(1.0, mix(1.0, 0.15, puddle), clamp(uPuddles.x, 0.0, 1.0));
        vec3 t = normalize(vTangent.xyz - n * dot(n, vTangent.xyz));
        vec3 b = cross(n, t) * (vTangent.w < 0.0 ? -1.0 : 1.0);
        n = normalize(t * tn.x + b * tn.y + n * max(tn.z, 0.2));
        uv += tn.xy * 0.03;
    }
    vec3 reflection = texture(uReflection, clamp(uv, vec2(0.001), vec2(0.999))).rgb * share;
    vec3 v = normalize(uCameraPosition - vWorld);
    float c = clamp(dot(n, v), 0.0, 1.0);
    float f = uFresnel == 1 ? uF0 + (1.0 - uF0) * pow(1.0 - c, 5.0) : uF0;
    vec3 colour = reflection * uTint * f;
    if (uPuddles.x > 2.5) { fragColor = vec4(fract(vWorld.x * 0.2), fract(vWorld.z * 0.2), step(0.0, r.w), 1.0); return; }   // CNA_ROOM_DEBUG_PUDDLES=2: world xz stripes, blue = w > 0
    if (uPuddles.x > 1.5) { fragColor = vec4(puddle, 0.0, 0.5, 1.0); return; }   // CNA_ROOM_DEBUG_PUDDLES: the mask in red over blue
    if (uHasEmissive == 1) {
        vec2 euv = vUv * uUvScale;
        if (uEmissiveFlipV == 1) euv.y = 1.0 - euv.y;
        colour += decodeSrgb(texture(uEmissive, euv).rgb) * uEmissiveFactor;
    }
    if (uEncodeSrgb == 1) colour = encodeSrgb(clamp(colour, 0.0, 1.0));
    fragColor = vec4(colour, 1.0);
}
)";

}  // namespace

PlanarReflection::PlanarReflection(GraphicsDevice& device, int width, int height) : device_(device)
{
    if (!device_.SupportsCapability(CNA::GraphicsCapability::CustomEffects) || !device_.ExecutesShaderEffectSourceEXT())
    {
        reason_ = "the renderer does not execute shader-effect source";
        return;
    }
    effect_ = std::make_unique<ShaderEffect>(device_, kVertexSource, kFragmentSource);
    if (!effect_->IsEffectValid())
    {
        reason_ = "the reflection shader did not compile: " + effect_->GetCompileErrorEXT();
        CNA::Logger::Error("cna-room: " + reason_);
        effect_.reset();
        return;
    }
    black_ = std::make_unique<Texture2D>(device_, 1, 1);
    const Color pixel = Color::Black;
    black_->SetData(&pixel, 1);
    supported_ = true;
    resize(width, height);
}

PlanarReflection::~PlanarReflection() = default;

void PlanarReflection::resize(int width, int height)
{
    if (!supported_) return;
    width_ = std::max(16, width);
    height_ = std::max(16, height);
    // HdrBlendable, not HalfVector4: the target below is drawn into with an additive blend
    // (light sources' contribution to the reflection), and HiDef permits render-target blending
    // for HdrBlendable but not for HalfVector4, even though CNA backs both with the same storage.
    const SurfaceFormat format = device_.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable) ? SurfaceFormat::HdrBlendable
                                                                                                             : SurfaceFormat::Color;
    target_ = std::make_unique<RenderTarget2D>(device_, width_, height_, false, format, DepthFormat::Depth24, 0,
                                               RenderTargetUsage::DiscardContents);
    valid_ = false;
}

bool PlanarReflection::prepare(const ReflectionPlane& plane, const Camera& camera)
{
    valid_ = false;
    if (!supported_) return false;
    Plane p = Plane::Normalize(plane.plane);
    const Vector3 eye = camera.position();
    const float side = Vector3::Dot(p.Normal, eye) + p.D;
    if (side <= 0.02f) return false;   // behind or on the glass
    // The surface in view? Its corners' box against the frustum: a street
    // filling the frame has no corner inside it and every edge crossing it.
    const BoundingFrustum& frustum = camera.frustum();
    Vector3 lo = plane.corners[0], hi = plane.corners[0];
    for (const Vector3& corner : plane.corners)
    {
        lo = Vector3::Min(lo, corner);
        hi = Vector3::Max(hi, corner);
    }
    const Vector3 pad(0.05f, 0.05f, 0.05f);
    bool inView = frustum.Contains(BoundingBox(lo - pad, hi + pad)) != ContainmentType::Disjoint;
    // A surface spanning the view has no corner inside it: also accept a
    // camera close to the plane looking at it.
    if (!inView)
    {
        Vector3 centre = Vector3::Zero;
        for (const Vector3& corner : plane.corners) centre = centre + corner * 0.25f;
        const Vector3 toCentre = centre - eye;
        if (Vector3::Dot(toCentre, camera.forward()) > 0.0f && toCentre.Length() < 4.0f) inView = true;
    }
    if (!inView) return false;

    // Mirrored view: the world reflected in the plane, seen by the unmoved
    // camera (the room's reflection lies beyond the glass).
    view_ = ReflectionMath::reflectedView(camera.view(), p);
    mirroredPosition_ = Vector3::Transform(eye, Matrix::CreateReflection(p));
    planeNormal_ = p.Normal;

    // Off-centre projection through the plane's corners: only the glass is
    // ever looked up, so the capture is tightened to it (fewer items drawn,
    // more texels per centimetre of reflection). Falls back to the whole
    // frame when a corner is behind the camera; off screen means no capture.
    Matrix projection;
    {
        float x0 = 1.0f, x1 = -1.0f, y0 = 1.0f, y1 = -1.0f;
        bool behind = false;
        const Matrix viewBase = view_ * camera.projection();
        for (const Vector3& corner : plane.corners)
        {
            const Vector4 c = Vector4::Transform(Vector4(corner.X, corner.Y, corner.Z, 1.0f), viewBase);
            if (c.W <= 0.01f) { behind = true; break; }
            x0 = std::min(x0, c.X / c.W); x1 = std::max(x1, c.X / c.W);
            y0 = std::min(y0, c.Y / c.W); y1 = std::max(y1, c.Y / c.W);
        }
        if (!behind && (x1 < -1.0f || x0 > 1.0f || y1 < -1.0f || y0 > 1.0f)) return false;
        (void)ReflectionMath::tightenProjection(view_, camera.projection(), plane.corners, camera.nearPlane(), camera.farPlane(), 0.03f,
                                                projection);
    }

    // Oblique near plane on the glass. The world is reflected and the camera
    // stays, so the visible half-space is the one the plane normal points
    // away from: the plane is handed over flipped. EasyGL feeds the XNA
    // matrix to GL unchanged (its shaders do gl_Position = WVP * position)
    // and GL clips at NDC z = -1, not at the 0 the D3D-style matrix maps
    // its own near plane to; the capture's depth buffer is its own, so the
    // different range from the main pass does not matter.
    Plane visibleSide(-p.Normal, -p.D);
    projection = ReflectionMath::obliqueProjection(projection, view_, visibleSide, -1.0f);
    // The remap keeps the side of the plane that holds the far corner it
    // picks, which is the room's side for a wall facing the camera but the
    // ground's side for a street seen from above: when a point half a metre
    // on the camera's side of the surface (the quad's centre lifted off it)
    // comes out clipped, the plane is handed over the other way round.
    {
        Vector3 centre = Vector3::Zero;
        for (const Vector3& corner : plane.corners) centre = centre + corner * 0.25f;
        const Vector3 probe = centre + p.Normal * 0.5f;
        const Vector3 probeView = Vector3::Transform(probe, view_);
        const Vector4 probeClip = Vector4::Transform(Vector4(probeView.X, probeView.Y, probeView.Z, 1.0f), projection);
        if (probeClip.W > 0.01f && probeClip.Z / probeClip.W < -1.0f)
        {
            visibleSide = Plane(p.Normal, p.D);
            projection = ReflectionMath::obliqueProjection(camera.projection(), view_, visibleSide, -1.0f);
            if (!ReflectionMath::tightenProjection(view_, camera.projection(), plane.corners, camera.nearPlane(), camera.farPlane(), 0.03f,
                                                   projection))
                projection = camera.projection();
            projection = ReflectionMath::obliqueProjection(projection, view_, visibleSide, -1.0f);
        }
    }
    if (!loggedOblique_)
    {
        loggedOblique_ = true;
        const Vector3 pointOnPlane = p.Normal * (-p.D);
        CNA::Logger::Info("cna-room: planar reflection oblique projection: plane point -> "
                          + std::to_string(ReflectionMath::ndcDepth(pointOnPlane, view_, projection)) + ", room side 0.5 m -> "
                          + std::to_string(ReflectionMath::ndcDepth(pointOnPlane + p.Normal * 0.5f, view_, projection)) + ", behind 0.05 m -> "
                          + std::to_string(ReflectionMath::ndcDepth(pointOnPlane - p.Normal * 0.05f, view_, projection)) + " (mirrored eye "
                          + std::to_string(mirroredPosition_.X) + "," + std::to_string(mirroredPosition_.Y) + "," + std::to_string(mirroredPosition_.Z) + ")");
    }
    projection_ = projection;
    if (std::getenv("CNA_ROOM_DEBUG_REFLECTIONS") != nullptr)
        CNA::Logger::Info("cna-room: reflection '" + plane.name + "' projection M11 " + std::to_string(projection_.M11) + " M22 "
                          + std::to_string(projection_.M22) + " M31 " + std::to_string(projection_.M31) + " M32 " + std::to_string(projection_.M32)
                          + " (camera M11 " + std::to_string(camera.projection().M11) + " M22 " + std::to_string(camera.projection().M22) + ")");
    viewProjection_ = view_ * projection_;
    frustum_ = BoundingFrustum(viewProjection_);
    valid_ = true;
    return true;
}

void PlanarReflection::beginCapture()
{
    device_.SetRenderTarget(target_.get());
    device_.Clear(Color::Black, 1.0f);
}

void PlanarReflection::endCapture()
{
    device_.SetRenderTarget(nullptr);
}

void PlanarReflection::drawSurface(const Material& material, const Matrix& world, const Matrix& view, const Matrix& projection,
                                   const Vector3& cameraPosition, const GpuMesh& mesh, bool encodeSrgb, bool additive)
{
    if (!supported_) return;
    float w[16], vp[16], rvp[16];
    world.ToColumnMajor(w);
    (view * projection).ToColumnMajor(vp);
    viewProjection_.ToColumnMajor(rvp);
    device_.setBlendStateProperty(additive ? BlendState::Additive : BlendState::Opaque);
    device_.setDepthStencilStateProperty(additive ? DepthStencilState::DepthRead : DepthStencilState::Default);
    device_.setRasterizerStateProperty(material.doubleSided ? RasterizerState::CullNone
                                       : material.frontFaceCounterClockwise ? RasterizerState::CullClockwise
                                                                            : RasterizerState::CullCounterClockwise);
    // Slot 0 is uReflection: target_, an HDR-format (HdrBlendable/HalfVector4) render target.
    // HiDef only permits point sampling of float/half formats -- see
    // GraphicsProfileDrawStateFormatTest's FloatAndHalfTexturesRequirePurePointFiltering -- so
    // this one slot stays PointClamp while the material's ordinary Color-format textures below
    // keep their linear filtering.
    device_.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    device_.getSamplerStatesProperty()[1] = SamplerState::LinearClamp;
    device_.getSamplerStatesProperty()[2] = SamplerState::LinearWrap;
    effect_->Apply();
    effect_->SetUniformMat4("uWorld", w);
    effect_->SetUniformMat4("uViewProjection", vp);
    effect_->SetUniformMat4("uReflectionViewProjection", rvp);
    effect_->SetUniformVec3("uCameraPosition", cameraPosition.X, cameraPosition.Y, cameraPosition.Z);
    effect_->SetUniformVec3("uTint", material.reflectionTint.X, material.reflectionTint.Y, material.reflectionTint.Z);
    effect_->SetUniformVec3("uEmissiveFactor", material.emissiveFactor.X, material.emissiveFactor.Y, material.emissiveFactor.Z);
    effect_->SetUniformVec2("uUvScale", material.uvScale.X, material.uvScale.Y);
    effect_->SetUniformFloat("uF0", material.reflectionF0);
    static const int debugPuddles = std::getenv("CNA_ROOM_DEBUG_PUDDLES") != nullptr ? std::atoi(std::getenv("CNA_ROOM_DEBUG_PUDDLES")) : 0;
    effect_->SetUniformVec2("uPuddles", debugPuddles > 0 && material.reflectionPuddles > 0.0f ? (debugPuddles >= 2 ? 3.0f : 2.0f) : material.reflectionPuddles,
                            material.reflectionPuddleScale);
    // An overlay is drawn over the same triangles another shader just wrote:
    // its depth can land a few quanta behind theirs and fail the equal test
    // (the road did, the pavement did not), so it steps a centimetre toward
    // the camera. The lookup keeps the true position.
    effect_->SetUniformFloat("uNudge", additive && material.reflectionOverlay ? 0.01f : 0.0f);
    effect_->SetUniformInt("uFresnel", material.reflectionFresnel ? 1 : 0);
    effect_->SetUniformInt("uHasEmissive", material.emissive != nullptr ? 1 : 0);
    effect_->SetUniformInt("uEncodeSrgb", encodeSrgb ? 1 : 0);
    effect_->SetUniformInt("uFlipV", material.reflectionFlipV ? 1 : 0);
    effect_->SetUniformInt("uEmissiveFlipV", material.reflectionEmissiveFlipV ? 1 : 0);
    effect_->SetUniformInt("uReflection", 0);
    effect_->SetUniformInt("uEmissive", 1);
    effect_->SetUniformInt("uNormalMap", 2);
    effect_->SetUniformInt("uHasNormal", material.normal != nullptr ? 1 : 0);
    effect_->SetUniformFloat("uNormalScale", material.normalScale);
    effect_->SetTexture(0, valid_ ? static_cast<Texture2D&>(*target_) : *black_);
    effect_->SetTexture(1, material.emissive != nullptr ? *material.emissive : *black_);
    effect_->SetTexture(2, material.normal != nullptr ? *material.normal : *black_);
    mesh.draw(device_);
}

}  // namespace CnaRoom
