// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Effects/PostProcessPass.hpp"
#include "CnaRoom/Effects/FullscreenPass.hpp"
#include <memory>

namespace Microsoft::Xna::Framework::Graphics { class ShaderEffect; }

namespace CnaRoom::Effects {

/// Contains non-finite scene radiance before spatial filters spread it across the frame.
class FiniteHdrPass final : public PostProcessPass
{
public:
    explicit FiniteHdrPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~FiniteHdrPass() override;
    void apply(const PostProcessContext& context) override;
    [[nodiscard]] const std::string& getName() const override;
    [[nodiscard]] bool isSupported(Microsoft::Xna::Framework::Graphics::GraphicsDevice&) const override;

private:
    FullscreenPass fullscreen_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
};

} // namespace CnaRoom::Effects
