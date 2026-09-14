// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/Vignette.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
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
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <cstdint>
#include <exception>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

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

// The factor the frame is multiplied by: 1 in the middle, 1 - strength in
// the corners, on a gentle curve (the frame is already display-encoded, so a
// linear falloff would read too heavy).
constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 vUv;
uniform float uStrength;
uniform float uAspect;
uniform vec3 uGain;
out vec4 fragColor;
void main() {
    vec2 p = (vUv * 2.0 - 1.0) * vec2(1.0, 1.0 / max(uAspect, 0.1)) * vec2(1.0, 1.0);
    p.y *= uAspect;
    float r = length(p) / length(vec2(1.0, uAspect));
    float fall = smoothstep(0.35, 1.05, r);
    float factor = 1.0 - uStrength * fall * fall;
    fragColor = vec4(vec3(factor) * uGain, 1.0);
}
)";

}  // namespace

Vignette::Vignette(GraphicsDevice& device) : device_(device)
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
            reason_ = "the vignette shader did not compile: " + effect_->GetCompileErrorEXT();
            effect_.reset();
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
        multiply_ = std::make_unique<BlendState>();
        multiply_->setColorSourceBlendProperty(Blend::Zero);
        multiply_->setColorDestinationBlendProperty(Blend::SourceColor);
        multiply_->setAlphaSourceBlendProperty(Blend::Zero);
        multiply_->setAlphaDestinationBlendProperty(Blend::One);
        supported_ = true;
    }
    catch (const std::exception& error)
    {
        reason_ = std::string("vignette setup threw: ") + error.what();
        CNA::Logger::Warn("living-room-simulator: " + reason_);
    }
}

Vignette::~Vignette() = default;

void Vignette::draw(float strength, int width, int height, const Vector3& gain)
{
    const bool neutral = gain.X >= 0.999f && gain.Y >= 0.999f && gain.Z >= 0.999f;
    if (!supported_ || (strength <= 0.0f && neutral)) return;
    device_.setBlendStateProperty(*multiply_);
    device_.setDepthStencilStateProperty(DepthStencilState::None);
    device_.setRasterizerStateProperty(RasterizerState::CullNone);
    effect_->Apply();
    effect_->SetUniformFloat("uStrength", std::max(strength, 0.0f));
    effect_->SetUniformVec3("uGain", gain.X, gain.Y, gain.Z);
    effect_->SetUniformFloat("uAspect", height > 0 ? static_cast<float>(height) / static_cast<float>(width) : 1.0f);
    device_.SetVertexBuffer(quad_.get());
    device_.setIndicesProperty(quadIndices_.get());
    device_.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
    device_.SetVertexBuffer(nullptr);
    device_.setIndicesProperty(nullptr);
    device_.setBlendStateProperty(BlendState::Opaque);
}

}  // namespace CnaRoom

