// SPDX-License-Identifier: MS-PL
#include "CnaRoom/Effects/AutoExposureEXT.hpp"

#include "CnaRoom/Effects/RenderPipelineSettings.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CnaRoom::Effects {

    float AutoExposureEXT::update(const float averageLuminance, const float deltaSeconds)
    {
        // The meter's reading. Before 2026-10-13 this class reduced the frame itself with a
        // compute shader: 4096 point samples on a 64 x 64 grid, each floored at 1e-4 before the
        // log. The meter's plain reading is the same geometric mean over 64 x 36 cells, each the
        // box mean of 64 taps and floored at 1e-5 -- averaging before the log lifts a cell with
        // internal contrast slightly (log of a mean is at least the mean of logs), and the lower
        // floor lets a black frame read darker. In practice the two agree to a few percent; the
        // key value is calibrated against the meter.
        const float average = std::isfinite(averageLuminance) ? std::max(averageLuminance, 0.0f) : 0.0f;
        const float target = std::clamp(keyValue_ / std::max(average, 1e-4f), minimumExposure_,
                                        maximumExposure_);

        if (deltaSeconds <= 0.0f)
        {
            exposure_ = target;
            return exposure_;
        }

        // Exponential approach, with the eye's own asymmetry: adapting to a brighter scene is
        // fast, adapting to a darker one is slow. A camera that snapped to each frame would
        // strobe. Note which comparison that is -- a BRIGHTER scene needs a LOWER exposure, so the
        // fast direction is the target falling, not rising. Getting this backwards is invisible in
        // a still frame and obvious the moment anything moves.
        const float speed = target < exposure_ ? brighteningSpeed_ : darkeningSpeed_;
        const float blend = 1.0f - std::exp(-speed * deltaSeconds);
        exposure_ = std::clamp(exposure_ + (target - exposure_) * blend, minimumExposure_,
                               maximumExposure_);
        return exposure_;
    }

    void AutoExposureEXT::applyTo(RenderPipelineSettings& settings) const
    {
        settings.setExposure(exposure_);
    }

    float AutoExposureEXT::getExposure() const { return exposure_; }

    void AutoExposureEXT::setExposure(const float value)
    {
        if (!(value > 0.0f))
            throw std::invalid_argument(
                "CnaRoom::Effects::AutoExposureEXT::setExposure: the exposure must be positive");
        exposure_ = std::clamp(value, minimumExposure_, maximumExposure_);
    }

    void AutoExposureEXT::setKeyValue(const float value)
    {
        if (!(value > 0.0f))
            throw std::invalid_argument(
                "CnaRoom::Effects::AutoExposureEXT::setKeyValue: the key value must be positive");
        keyValue_ = value;
    }

    float AutoExposureEXT::getKeyValue() const { return keyValue_; }

    void AutoExposureEXT::setAdaptationSpeeds(const float brighteningPerSecond,
                                              const float darkeningPerSecond)
    {
        if (!(brighteningPerSecond > 0.0f) || !(darkeningPerSecond > 0.0f))
            throw std::invalid_argument(
                "CnaRoom::Effects::AutoExposureEXT::setAdaptationSpeeds: both speeds must be positive");
        brighteningSpeed_ = brighteningPerSecond;
        darkeningSpeed_ = darkeningPerSecond;
    }

    float AutoExposureEXT::getBrighteningSpeed() const { return brighteningSpeed_; }

    float AutoExposureEXT::getDarkeningSpeed() const { return darkeningSpeed_; }

    void AutoExposureEXT::setExposureRange(const float minimum, const float maximum)
    {
        if (!(minimum > 0.0f) || maximum < minimum)
            throw std::invalid_argument(
                "CnaRoom::Effects::AutoExposureEXT::setExposureRange: need 0 < minimum <= maximum");
        minimumExposure_ = minimum;
        maximumExposure_ = maximum;
        exposure_ = std::clamp(exposure_, minimumExposure_, maximumExposure_);
    }

    float AutoExposureEXT::getMinimumExposure() const { return minimumExposure_; }

    float AutoExposureEXT::getMaximumExposure() const { return maximumExposure_; }

} // namespace CnaRoom::Effects
