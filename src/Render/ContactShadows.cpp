// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/ContactShadows.hpp"

#include "CNA/Graphics/ContactShadowPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;

namespace CnaRoom {
namespace {

constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;
out vec2 vUv;
void main() { gl_Position = vec4(aPosition.xy, 0.0, 1.0); vUv = aTexCoord; }
)";

constexpr const char* kCopyFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vUv;
uniform sampler2D uMask;
uniform sampler2D uNormals;
uniform vec3 uLightView;   // toward the light, view space
uniform int uRaw;          // 1: the mask as marched, no facing test
out vec4 fragColor;
// The pass draws through CNA's FullscreenPass, whose output is mirrored in V
// against this quad's UV origin (CNA_FINDINGS R-8): the mask is sampled
// flipped, the prepass normals as they are. Without a normal the pass shadows
// every surface facing away from the light with its own depth (the ray dips
// behind the surface within the thickness), which would darken the ambient on
// the whole unlit side of the room; the mask is applied only where the surface
// faces the light, fading in over the first quarter of the cosine.
void main() {
    float mask = texture(uMask, vec2(vUv.x, 1.0 - vUv.y)).r;
    vec3 n = normalize(texture(uNormals, vUv).xyz * 2.0 - 1.0);
    float facing = uRaw == 1 ? 1.0 : smoothstep(0.0, 0.25, dot(n, uLightView));
    float visibility = mix(1.0, mask, facing);
    fragColor = vec4(vec3(visibility), 1.0);
}
)";

}  // namespace

ContactShadows::ContactShadows(GraphicsDevice& device) : device_(device)
{
    try
    {
        pass_ = std::make_unique<CNA::Graphics::ContactShadowPass>(device_);
        if (!pass_->isSupported(device_))
        {
            reason_ = "the contact shadow pass is not supported on this renderer";
            pass_.reset();
            return;
        }
        copyEffect_ = std::make_unique<ShaderEffect>(device_, kVertexSource, kCopyFragmentSource);
        if (!copyEffect_->IsEffectValid())
        {
            reason_ = "the mask shader did not compile: " + copyEffect_->GetCompileErrorEXT();
            copyEffect_.reset();
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
        white_ = std::make_unique<Texture2D>(device_, 1, 1);
        const Color white = Color::White;
        white_->SetData(&white, 1);
        // Zero * source + destination * mask: the lit scene scaled by the visibility.
        multiply_ = std::make_unique<BlendState>();
        multiply_->setColorSourceBlendProperty(Blend::Zero);
        multiply_->setColorDestinationBlendProperty(Blend::SourceColor);
        multiply_->setAlphaSourceBlendProperty(Blend::Zero);
        multiply_->setAlphaDestinationBlendProperty(Blend::One);
        supported_ = true;
    }
    catch (const std::exception& e)
    {
        reason_ = std::string("contact shadows failed to initialise: ") + e.what();
        pass_.reset();
        supported_ = false;
    }
}

ContactShadows::~ContactShadows() = default;

bool ContactShadows::ensureMask(int width, int height)
{
    if (mask_ != nullptr && maskWidth_ == width && maskHeight_ == height) return true;
    if (maskFailed_) return false;
    try
    {
        mask_ = std::make_unique<RenderTarget2D>(device_, width, height, false, SurfaceFormat::Color, DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        maskWidth_ = width;
        maskHeight_ = height;
        return true;
    }
    catch (const std::exception& e)
    {
        CNA::Logger::Warn(std::string("living-room-simulator: the contact shadow mask could not be created: ") + e.what());
        maskFailed_ = true;
        return false;
    }
}

void ContactShadows::drawQuad()
{
    device_.SetVertexBuffer(quad_.get());
    device_.setIndicesProperty(quadIndices_.get());
    device_.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
    device_.SetVertexBuffer(nullptr);
    device_.setIndicesProperty(nullptr);
}

void ContactShadows::march(const Inputs& in, int width, int height)
{
    marched_ = false;
    if (!supported_ || in.depth == nullptr || in.intensity <= 0.0f || width <= 0 || height <= 0) return;
    if (!ensureMask(width, height)) return;
    debug_ = in.debug;
    normals_ = in.normals;
    // The pass takes the direction the light travels; the facing test wants the direction toward it.
    lightView_ = Vector3::TransformNormal(Vector3(-in.lightDirection.X, -in.lightDirection.Y, -in.lightDirection.Z), in.view);
    if (lightView_.Length() > 1e-6f) lightView_.Normalize();
    pass_->setLightDirection(in.lightDirection);
    pass_->setMaxDistance(in.maxDistance);
    pass_->setThickness(in.thickness);
    pass_->setBias(in.bias);
    pass_->setIntensity(std::clamp(in.intensity, 0.0f, 1.0f));
    pass_->setStepCount(in.steps);
    CNA::Graphics::PostProcessContext context;
    context.source = white_.get();
    context.destination = mask_.get();
    context.width = width;
    context.height = height;
    context.sourceDepth = in.depth;
    context.projection = in.projection;
    context.inverseProjection = in.inverseProjection;
    context.nearPlane = in.nearPlane;
    context.farPlane = in.farPlane;
    context.inverseView = in.inverseView;
    pass_->apply(context);
    device_.SetRenderTarget(nullptr);
    device_.setBlendStateProperty(BlendState::Opaque);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    if (!pass_->getFallbackReason().empty())
    {
        if (!loggedFallback_) CNA::Logger::Warn("living-room-simulator: contact shadows copied through: " + pass_->getFallbackReason());
        loggedFallback_ = true;
        return;
    }
    marched_ = true;
}

void ContactShadows::apply()
{
    if (!marched_ || normals_ == nullptr) { marched_ = false; return; }
    marched_ = false;
    device_.setBlendStateProperty(debug_ != 0 ? BlendState::Opaque : *multiply_);
    device_.setDepthStencilStateProperty(DepthStencilState::None);
    device_.setRasterizerStateProperty(RasterizerState::CullNone);
    copyEffect_->Apply();
    copyEffect_->SetUniformInt("uMask", 0);
    copyEffect_->SetTexture(0, static_cast<Texture2D&>(*mask_));
    copyEffect_->SetUniformInt("uNormals", 1);
    if (normals_ != nullptr) copyEffect_->SetTexture(1, *normals_);
    copyEffect_->SetUniformVec3("uLightView", lightView_.X, lightView_.Y, lightView_.Z);
    copyEffect_->SetUniformInt("uRaw", debug_ == 2 ? 1 : 0);
    drawQuad();
    device_.setBlendStateProperty(BlendState::Opaque);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
}

}  // namespace CnaRoom
