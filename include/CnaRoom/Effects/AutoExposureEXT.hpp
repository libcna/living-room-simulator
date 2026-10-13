// SPDX-License-Identifier: MS-PL
#pragma once

namespace CnaRoom::Effects {

/** @addtogroup cnaext_engine
 *  @{
 */

    class RenderPipelineSettings;

    /**
     * @brief Turns a frame's average luminance into an exposure, with the eye's own adaptation.
     *
     * The measurement is not made here. `ExposureMeter` reads the HDR scene once per frame and
     * hands its plain geometric-mean luminance to @ref update; this class is the small amount of
     * control theory around that number, and nothing in it touches the graphics device.
     *
     * **Log-average**, not a plain mean: the meter's reading is a geometric mean, because a few very
     * bright pixels would otherwise drag the whole frame dark, which is exactly the artefact
     * auto-exposure is blamed for.
     *
     * **Adaptation is deliberate and asymmetric.** An eye adjusts to darkness far more slowly than
     * to light, and a camera that snapped instantly to each frame's luminance would strobe on every
     * muzzle flash. @ref update moves the exposure toward its target exponentially, with separate
     * speeds for brightening and darkening.
     */
    class AutoExposureEXT
    {
    public:
        /** @brief Creates the controller at exposure 1, key 0.18, range 0.01..64. */
        AutoExposureEXT() = default;

        /**
         * @brief Feeds one frame's measured luminance and moves the exposure toward what it asks for.
         *
         * @param averageLuminance The frame's log-average (geometric mean) luminance in scene units.
         *                         A value that is not finite counts as black.
         * @param deltaSeconds     Time since the last call; zero or less snaps straight to the target.
         * @return The exposure to render the next frame with.
         */
        float update(float averageLuminance, float deltaSeconds);

        /**
         * @brief Writes the current exposure into a settings bag.
         *
         * The whole integration with the pipeline: `TonemapPass` already reads
         * `RenderPipelineSettings::getExposure()` every frame, so nothing else has to change.
         *
         * @param settings The settings to write to.
         */
        void applyTo(RenderPipelineSettings& settings) const;

        /** @brief Returns the exposure as it stands. */
        [[nodiscard]] float getExposure() const;

        /**
         * @brief Sets the exposure directly, skipping adaptation.
         *
         * @param value The exposure; must be positive.
         * @throws std::invalid_argument If @p value is not positive.
         */
        void setExposure(float value);

        /**
         * @brief Sets the middle-grey the exposure aims to put the frame's log-average at.
         *
         * @param value Photography's 0.18 by default; must be positive.
         * @throws std::invalid_argument If @p value is not positive.
         */
        void setKeyValue(float value);

        /** @brief Returns the target middle-grey. */
        [[nodiscard]] float getKeyValue() const;

        /**
         * @brief Sets how fast the exposure adapts, in units of e-foldings per second.
         *
         * The speeds are named for the **scene**, not for the exposure: a brighter scene needs a
         * lower exposure, so `brighteningPerSecond` is the speed at which the exposure comes
         * *down*. That is the eye's fast direction.
         *
         * @param brighteningPerSecond Speed when the scene has become brighter; must be positive.
         * @param darkeningPerSecond   Speed when it has become darker; must be positive.
         * @throws std::invalid_argument If either is not positive.
         */
        void setAdaptationSpeeds(float brighteningPerSecond, float darkeningPerSecond);

        /** @brief Returns the brightening adaptation speed. */
        [[nodiscard]] float getBrighteningSpeed() const;

        /** @brief Returns the darkening adaptation speed. */
        [[nodiscard]] float getDarkeningSpeed() const;

        /**
         * @brief Clamps the exposure the adaptation may reach.
         *
         * @param minimum Lowest allowed exposure; must be positive.
         * @param maximum Highest allowed exposure; must be at least @p minimum.
         * @throws std::invalid_argument If the range is not valid.
         */
        void setExposureRange(float minimum, float maximum);

        /** @brief Returns the lowest exposure the adaptation may reach. */
        [[nodiscard]] float getMinimumExposure() const;

        /** @brief Returns the highest exposure the adaptation may reach. */
        [[nodiscard]] float getMaximumExposure() const;

    private:
        float exposure_ = 1.0f;
        float keyValue_ = 0.18f;
        float brighteningSpeed_ = 3.0f;
        float darkeningSpeed_ = 1.0f;
        float minimumExposure_ = 0.01f;
        float maximumExposure_ = 64.0f;
    };

/** @} */ // end of cnaext_engine

} // namespace CnaRoom::Effects
