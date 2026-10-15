// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/ExposureMeter.hpp"

#include "CnaRoom/Effects/FullscreenPass.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "shaders/luminance_meter/LuminanceMeterShaderPackage.generated.hpp"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaRoom {

namespace {

constexpr int kCellsX = ExposureStatistics::kCellsX;
constexpr int kCellsY = ExposureStatistics::kCellsY;

// GLSL ES 3.00 and desktop GLSL 3.30 only: the simulator's renderers are
// OPENGLES3, OPENGL33 and WEBGL2, all of which EasyGL drives from these two.
CNA::Graphics::ShaderPackageEXT luminancePackage()
{
    using namespace CnaRoom::detail::LuminanceMeterGenerated;
    using CNA::Graphics::ShaderCodeEXT;
    using CNA::ShaderLanguageEXT;
    using CNA::ShaderStageEXT;
    return CNA::Graphics::ShaderPackageEXT(
        std::vector<ShaderCodeEXT>{
            {ShaderLanguageEXT::GlslEs, ShaderStageEXT::Vertex, "main", "luminance_meter/fullscreen.es.vert.glsl",
             std::string(kFullscreenEsVertexSource)},
            {ShaderLanguageEXT::GlslEs, ShaderStageEXT::Fragment, "main", "luminance_meter/luminance_cells.es.frag.glsl",
             std::string(kLuminanceCellsEsFragmentSource)},
            {ShaderLanguageEXT::GlslDesktop, ShaderStageEXT::Vertex, "main", "luminance_meter/fullscreen.desktop.vert.glsl",
             std::string(kFullscreenDesktopVertexSource)},
            {ShaderLanguageEXT::GlslDesktop, ShaderStageEXT::Fragment, "main", "luminance_meter/luminance_cells.desktop.frag.glsl",
             std::string(kLuminanceCellsDesktopFragmentSource)}},
        {ShaderStageEXT::Vertex, ShaderStageEXT::Fragment},
        {{"texture1", 0, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D, ShaderStageEXT::Fragment}});
}

}  // namespace

ExposureMeter::ExposureMeter(GraphicsDevice& device) : device_(device)
{
    try
    {
        if (!device_.SupportsCapability(CNA::GraphicsCapability::CustomEffects) || !device_.ExecutesShaderEffectSourceEXT())
        {
            reason_ = "the renderer does not execute shader-effect source";
            return;
        }
        const CNA::Graphics::ShaderPackageEXT package = luminancePackage();
        if (!package.selectFor(device_).isUsable())
        {
            reason_ = "no luminance shader variant for this renderer";
            return;
        }
        effect_ = std::make_unique<ShaderEffect>(device_, package);
        if (!effect_->IsEffectValid())
        {
            reason_ = "the luminance shader did not compile: " + effect_->GetCompileErrorEXT();
            effect_.reset();
            return;
        }
        pass_ = std::make_unique<CnaRoom::Effects::FullscreenPass>(device_);
        // RGBA8 so the readback is the one every renderer supports; the cells
        // carry a 24-bit log2 code (ExposureStatistics), not a colour.
        cells_ = std::make_unique<RenderTarget2D>(device_, kCellsX, kCellsY, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                                  RenderTargetUsage::DiscardContents);
        texels_.assign(static_cast<std::size_t>(kCellsX * kCellsY), Color());
        luminance_.assign(static_cast<std::size_t>(kCellsX * kCellsY), 0.0f);
        supported_ = true;
    }
    catch (const std::exception& error)
    {
        reason_ = std::string("luminance meter unavailable: ") + error.what();
        CNA::Logger::Warn("room-simulator: " + reason_);
        effect_.reset();
        pass_.reset();
        cells_.reset();
    }
}

ExposureMeter::~ExposureMeter() = default;

ExposureMeter::Reading ExposureMeter::measure(RenderTarget2D& scene)
{
    if (!supported_) return Reading{};
    try
    {
        // Linear taps where the source allows them; FullscreenPass points the
        // sampler for a float source, which the cell's 8 x 8 taps cover anyway.
        pass_->draw(&scene, cells_.get(), effect_.get(), kCellsX, kCellsY,
                    const_cast<SamplerState*>(&SamplerState::LinearClamp));
        cells_->GetData(texels_.data(), static_cast<int>(texels_.size()));
        for (std::size_t i = 0; i < texels_.size(); ++i)
            luminance_[i] = ExposureStatistics::decodeLuminance(texels_[i].getRProperty(), texels_[i].getGProperty(),
                                                                texels_[i].getBProperty());
    }
    catch (const std::exception& error)
    {
        reason_ = std::string("exposure measurement failed: ") + error.what();
        CNA::Logger::Warn("room-simulator: " + reason_);
        supported_ = false;
        return Reading{};
    }
    return ExposureStatistics::evaluate(luminance_, kCellsX, kCellsY);
}

}  // namespace CnaRoom
