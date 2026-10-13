// SPDX-License-Identifier: MS-PL
#include "CnaRoom/Effects/PostProcessChain.hpp"

#include "CNA/Logger.hpp"


#include "CnaRoom/Effects/BlitPass.hpp"
#include "CnaRoom/Effects/PostProcessPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace CnaRoom::Effects {

    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

    PostProcessChain::PostProcessChain(GraphicsDevice& device)
        : device_(device), pool_(device), copyPass_(std::make_unique<BlitPass>(device))
    {
    }

    PostProcessChain::~PostProcessChain() = default;

    void PostProcessChain::addPass(PostProcessPass* pass)
    {
        if (pass != nullptr)
            passes_.push_back(pass);
    }

    void PostProcessChain::addOwnedPass(std::unique_ptr<PostProcessPass> pass)
    {
        if (!pass)
            return;
        passes_.push_back(pass.get());
        ownedPasses_.push_back(std::move(pass));
    }

    void PostProcessChain::clear()
    {
        passes_.clear();
        ownedPasses_.clear();
    }

    std::size_t PostProcessChain::getPassCount() const
    {
        return passes_.size();
    }

    void PostProcessChain::apply(const PostProcessContext& context)
    {
        if (context.source == nullptr)
            throw std::invalid_argument("CnaRoom::Effects::PostProcessChain::apply: source must not be null");
        if (context.width <= 0 || context.height <= 0)
            throw std::invalid_argument("CnaRoom::Effects::PostProcessChain::apply: size must be positive");

        const auto trace = [&](const std::string& name,
                               Microsoft::Xna::Framework::Graphics::Texture2D* target) {
            if (std::getenv("CNA_ROOM_TRACE_POST") == nullptr) return;
            const int count = context.width * context.height;
            double sum = 0.0;
            int invalid = 0;
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
            if (target != nullptr && (target->getFormatProperty() == SurfaceFormat::HalfVector4
                                     || target->getFormatProperty() == SurfaceFormat::HdrBlendable))
            {
                using Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector4;
                std::vector<HalfVector4> pixels(static_cast<std::size_t>(count));
                target->GetData(pixels.data(), count);
                for (const auto& pixel : pixels)
                {
                    const auto v = pixel.ToVector4();
                    if (!std::isfinite(v.X) || !std::isfinite(v.Y) || !std::isfinite(v.Z)) ++invalid;
                    else sum += (v.X + v.Y + v.Z) / 3.0;
                }
            }
            else
            {
                using Microsoft::Xna::Framework::Color;
                std::vector<Color> pixels(static_cast<std::size_t>(count));
                if (target != nullptr) target->GetData(pixels.data(), count);
                else device_.GetBackBufferData(pixels.data(), count);
                for (const auto& v : pixels)
                    sum += (v.getRProperty() + v.getGProperty() + v.getBProperty()) / (3.0 * 255.0);
            }
            CNA::Logger::Info("living-room-simulator: post trace " + name + " mean "
                + std::to_string(sum / count) + " invalid " + std::to_string(invalid));
        };
        trace("scene", context.source);

        if (passes_.empty())
        {
            copyPass_->apply(context);
            return;
        }

        // Intermediates carry the source's own format, so an HDR chain stays HDR until the pass
        // that deliberately leaves it -- putting a Color intermediate between two float passes
        // would clamp the frame invisibly, which is the failure this whole layer exists to avoid.
        const auto intermediateFormat = context.source->getFormatProperty();

        PostProcessContext step = context;
        for (std::size_t index = 0; index < passes_.size(); ++index)
        {
            const bool isLast = index + 1 == passes_.size();

            // The last pass writes the caller's real destination; every earlier one writes an
            // intermediate, alternating between two so no pass reads what it is writing.
            step.destination = isLast
                ? context.destination
                : pool_.acquire(context.width, context.height, intermediateFormat,
                                DepthFormat::None, static_cast<int>(index % 2));

            passes_[index]->apply(step);
            trace(passes_[index]->getName(), step.destination);

            // The next pass reads what this one just wrote.
            step.source = step.destination;
        }
    }

    void PostProcessChain::resetTargets()
    {
        pool_.reset();
    }

    const RenderTargetPool& PostProcessChain::getTargetPool() const
    {
        return pool_;
    }

    RenderTargetPool& PostProcessChain::getTargetPool() { return pool_; }

} // namespace CnaRoom::Effects
