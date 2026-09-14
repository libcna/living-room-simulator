// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

namespace CnaRoom {

enum class WeatherKind { Clear, Cloudy, Overcast, Rain, Storm, Snow, Hail };
enum class PrecipitationType { None, Rain, Snow, Hail };

/// The continuous outputs the renderer consumes; every value is smoothed.
struct WeatherState
{
    WeatherKind kind = WeatherKind::Cloudy;
    float cloudCoverage = 0.35f;
    float cloudDensity = 0.5f;
    float haze = 0.05f;
    float precipitation = 0.0f;      ///< 0..1 intensity of what is falling
    PrecipitationType type = PrecipitationType::None;
    float windSpeed = 2.0f;          ///< m/s
    float windDirection = 0.35f;     ///< radians, direction the wind blows towards (0 = +Z)
    float wetness = 0.0f;            ///< 0 dry .. 1 soaked surfaces
    float snowCover = 0.0f;          ///< 0 none .. 1 covered
    float lightning = 0.0f;          ///< flash 0..1 (this frame)
    float lightningAzimuth = 0.0f;   ///< where the last strike was, radians
    float temperatureC = 14.0f;
    float starVisibility = 1.0f;
};

/**
 * @brief A slowly evolving weather state machine.
 *
 * Kinds dwell for tens of game minutes and hand over to a neighbour on a
 * fixed transition table; the continuous parameters glide towards each
 * kind's targets at rates measured in game hours, so a front takes real
 * seconds to arrive at the default clock speed. Temperature has a diurnal
 * swing and drops under cloud; below about 1 °C the precipitation is snow,
 * and storms throw short hail bursts. Lightning is timed in real seconds
 * (a flash must be visible however fast the clock runs).
 */
class WeatherSystem
{
public:
    explicit WeatherSystem(std::uint32_t seed = 7u);

    /// Jumps to a kind (targets snap; continuous values glide unless `immediate`).
    void setKind(WeatherKind kind, bool immediate);
    /// Holds the current kind: no automatic transitions.
    void setHold(bool hold) { hold_ = hold; }
    [[nodiscard]] bool held() const { return hold_; }
    /// Overrides the temperature model (NaN restores the model).
    void setTemperatureOverride(float celsius) { temperatureOverride_ = celsius; }
    /// Speed multiplier for the state machine and the glides.
    void setSpeed(float multiplier) { speed_ = multiplier; }

    /// Advances by game hours (for the state machine and the surfaces) and
    /// real seconds (for lightning); `hourOfDay` feeds the temperature model.
    void advance(float gameHours, float realSeconds, float hourOfDay);
    void cycleKind();

    [[nodiscard]] const WeatherState& state() const { return state_; }
    [[nodiscard]] std::string describe() const;
    static const char* name(WeatherKind kind);
    static bool parse(const std::string& text, WeatherKind& kind);

private:
    struct Targets
    {
        float coverage, density, haze, precipitation, wind;
    };
    [[nodiscard]] static Targets targetsFor(WeatherKind kind);
    [[nodiscard]] WeatherKind pickNext();
    float random();

    WeatherState state_;
    WeatherKind target_ = WeatherKind::Cloudy;
    float dwellHours_ = 0.5f;
    float hold_ = false;
    float speed_ = 1.0f;
    float temperatureOverride_;
    std::uint32_t rng_;
    float nextFlashSeconds_ = 5.0f;
    float flashAge_ = 99.0f;
    float flashStrength_ = 0.0f;
    float hailBurstHours_ = 0.0f;
};

}  // namespace CnaRoom
