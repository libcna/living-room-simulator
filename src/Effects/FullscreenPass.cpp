// SPDX-License-Identifier: MS-PL
#include "CnaRoom/Effects/FullscreenPass.hpp"
#include "CnaRoom/Effects/ScopedRenderTarget.hpp"


#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <stdexcept>

namespace CnaRoom::Effects {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::SamplerState;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    FullscreenPass::FullscreenPass(GraphicsDevice& device)
        : device_(device), spriteBatch_(std::make_unique<SpriteBatch>(device))
    {
    }

    FullscreenPass::~FullscreenPass() = default;

    void FullscreenPass::draw(Texture2D* source, RenderTarget2D* destination, Effect* effect,
                              const int width, const int height, SamplerState* sampler)
    {
        if (source == nullptr)
            throw std::invalid_argument("CnaRoom::Effects::FullscreenPass::draw: source must not be null");
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("CnaRoom::Effects::FullscreenPass::draw: destination size must be positive");

        // plans/plan_modern.md MOD-203: bound for this scope only. If the draw throws -- a shader that
        // will not link, a SpriteBatch already inside a Begin -- the destination does not stay
        // bound, so the next thing to render does not silently draw into a pass's intermediate.
        ScopedRenderTarget bound(device_, destination);
        drawOverCurrentTarget(source, effect, width, height, sampler);
    }

    void FullscreenPass::drawOverCurrentTarget(Texture2D* source, Effect* effect, const int width,
                                               const int height, SamplerState* sampler)
    {
        if (source == nullptr)
            throw std::invalid_argument(
                "CnaRoom::Effects::FullscreenPass::drawOverCurrentTarget: source must not be null");
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CnaRoom::Effects::FullscreenPass::drawOverCurrentTarget: the size must be positive");

        // Opaque, not AlphaBlend: a post-process pass replaces the destination rather than
        // compositing onto it, and blending a pass's own output against whatever the target held
        // is a source of results that look almost right.
        // A null sampler means the device default, which is what every pass wanted before
        // MOD-220 and still wants unless it says otherwise.
        //
        // CNA enforces XNA's point sampling rule for float textures. The former
        // engine-only exemption is gone; select a legal sampler for HDR sources.
        SamplerState* effective = sampler;
        const auto format = source->getFormatProperty();
        switch (format)
        {
        case Microsoft::Xna::Framework::Graphics::SurfaceFormat::Single:
        case Microsoft::Xna::Framework::Graphics::SurfaceFormat::Vector2:
        case Microsoft::Xna::Framework::Graphics::SurfaceFormat::Vector4:
        case Microsoft::Xna::Framework::Graphics::SurfaceFormat::HalfSingle:
        case Microsoft::Xna::Framework::Graphics::SurfaceFormat::HalfVector2:
        case Microsoft::Xna::Framework::Graphics::SurfaceFormat::HalfVector4:
        case Microsoft::Xna::Framework::Graphics::SurfaceFormat::HdrBlendable:
            effective = const_cast<SamplerState*>(&SamplerState::PointClamp);
            break;
        default:
            break;
        }

        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            effective, nullptr, nullptr, effect);
        spriteBatch_->Draw(*source, Rectangle(0, 0, width, height), Color::White);
        spriteBatch_->End();
    }

} // namespace CnaRoom::Effects
