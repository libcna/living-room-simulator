// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/ExposureMeter.hpp"

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaRoom {

namespace {

constexpr int kCellsX = 64, kCellsY = 36;
constexpr int kGroupX = 8, kGroupY = 6;   // 8 x 6 groups of 8 x 6 threads = 64 x 36 cells

// One thread per cell: the mean of an 8 x 8 grid of bilinear taps over its
// share of the frame (a box filter good enough for statistics).
constexpr const char* kComputeSource = R"(#version 310 es
precision highp float;
precision highp sampler2D;
layout(local_size_x = 8, local_size_y = 6) in;
uniform sampler2D uScene;
layout(std430, binding = 1) writeonly buffer Cells
{
    float cells[];
};
void main()
{
    vec2 grid = vec2(gl_NumWorkGroups.xy * gl_WorkGroupSize.xy);
    vec2 cell = vec2(gl_GlobalInvocationID.xy);
    vec3 sum = vec3(0.0);
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i)
        {
            vec2 uv = (cell + (vec2(float(i), float(j)) + 0.5) / 8.0) / grid;
            sum += texture(uScene, uv).rgb;
        }
    vec3 mean = sum / 64.0;
    uint index = gl_GlobalInvocationID.y * uint(grid.x) + gl_GlobalInvocationID.x;
    cells[index] = dot(mean, vec3(0.2126, 0.7152, 0.0722));
}
)";

}  // namespace

ExposureMeter::ExposureMeter(GraphicsDevice& device) : device_(device)
{
    try
    {
        CNA::Graphics::ShaderPackageEXT package(
            {CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Compute, "main",
                                          "cna_room/exposure_cells.es.comp.glsl", std::string(kComputeSource))},
            {CNA::ShaderStageEXT::Compute},
            {CNA::Graphics::ShaderBindingRequirementEXT("uScene", 0, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                                                        CNA::ShaderStageEXT::Compute),
             CNA::Graphics::ShaderBindingRequirementEXT("Cells", 1, CNA::Graphics::ShaderBindingTypeEXT::StorageBuffer,
                                                        CNA::ShaderStageEXT::Compute)});
        reducer_ = std::make_unique<CNA::Graphics::ComputeShader>(device_, package);
        cells_ = std::make_unique<CNA::Graphics::StorageBufferT<float>>(device_, kCellsX * kCellsY);
        luminance_.assign(static_cast<std::size_t>(kCellsX * kCellsY), 0.0f);
        supported_ = true;
    }
    catch (const std::exception& error)
    {
        reason_ = std::string("compute meter unavailable: ") + error.what();
        CNA::Logger::Warn("living-room-simulator: " + reason_);
        reducer_.reset();
        cells_.reset();
    }
}

ExposureMeter::~ExposureMeter() = default;

ExposureMeter::Reading ExposureMeter::measure(RenderTarget2D& scene)
{
    Reading reading;
    if (!supported_) return reading;
    try
    {
        reducer_->bindTexture(0, "uScene", static_cast<Texture2D&>(scene));
        reducer_->bindStorageBuffer(1, cells_->getBuffer());
        reducer_->dispatch(kGroupX, kGroupY);
        const std::vector<float> cells = cells_->getData();
        if (cells.size() != luminance_.size()) throw std::runtime_error("cell count mismatch");
        for (std::size_t i = 0; i < cells.size(); ++i) luminance_[i] = std::max(cells[i], 0.0f);
    }
    catch (const std::exception& error)
    {
        CNA::Logger::Warn(std::string("living-room-simulator: exposure measurement failed: ") + error.what());
        supported_ = false;
        return reading;
    }

    // Statistics. A floor of 1e-5 keeps black cells from dominating the log.
    constexpr float kFloor = 1e-5f;
    double plainSum = 0.0;
    double weightSum = 0.0, weightedSum = 0.0;
    std::vector<float> weights(luminance_.size());
    for (int y = 0; y < kCellsY; ++y)
        for (int x = 0; x < kCellsX; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y * kCellsX + x);
            const float dx = (static_cast<float>(x) + 0.5f) / kCellsX * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / kCellsY * 2.0f - 1.0f;
            const float w = std::exp(-(dx * dx + dy * dy) / (2.0f * 0.55f * 0.55f));
            weights[i] = w;
            const double lg = std::log(static_cast<double>(std::max(luminance_[i], kFloor)));
            plainSum += lg;
            weightSum += w;
            weightedSum += w * lg;
        }
    reading.plain = static_cast<float>(std::exp(plainSum / static_cast<double>(luminance_.size())));
    const float firstPass = static_cast<float>(std::exp(weightedSum / std::max(weightSum, 1e-9)));
    // Highlights: cells more than 12x above the weighted mean (shades, bulbs,
    // a bright window by night) count a twentieth. Two passes settle it.
    float mean = firstPass;
    float share = 0.0f;
    for (int pass = 0; pass < 2; ++pass)
    {
        double ws = 0.0, wl = 0.0, rejected = 0.0;
        const float limit = mean * 12.0f;
        for (std::size_t i = 0; i < luminance_.size(); ++i)
        {
            const float l = std::max(luminance_[i], kFloor);
            float w = weights[i];
            if (l > limit)
            {
                rejected += w;
                w *= 0.05f;
            }
            ws += w;
            wl += w * std::log(static_cast<double>(l));
        }
        mean = static_cast<float>(std::exp(wl / std::max(ws, 1e-9)));
        share = static_cast<float>(rejected / std::max(weightSum, 1e-9));
    }
    reading.weighted = mean;
    reading.highlightShare = share;
    return reading;
}

}  // namespace CnaRoom
