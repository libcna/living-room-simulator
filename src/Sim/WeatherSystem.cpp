// SPDX-License-Identifier: MIT
#include "CnaRoom/Sim/WeatherSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace CnaRoom {

namespace {
constexpr float kPi = 3.14159265358979f;

float towards(float value, float target, float maxStep)
{
    if (value < target) return std::min(target, value + maxStep);
    return std::max(target, value - maxStep);
}
}  // namespace

WeatherSystem::WeatherSystem(std::uint32_t seed)
    : temperatureOverride_(std::nanf("")), rng_(seed * 747796405u + 2891336453u)
{
    setKind(WeatherKind::Cloudy, true);
}

float WeatherSystem::random()
{
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

const char* WeatherSystem::name(WeatherKind kind)
{
    switch (kind)
    {
        case WeatherKind::Clear: return "clear";
        case WeatherKind::Cloudy: return "cloudy";
        case WeatherKind::Overcast: return "overcast";
        case WeatherKind::Rain: return "rain";
        case WeatherKind::Storm: return "storm";
        case WeatherKind::Snow: return "snow";
        case WeatherKind::Hail: return "hail";
    }
    return "?";
}

bool WeatherSystem::parse(const std::string& text, WeatherKind& kind)
{
    for (int k = 0; k <= static_cast<int>(WeatherKind::Hail); ++k)
        if (text == name(static_cast<WeatherKind>(k)))
        {
            kind = static_cast<WeatherKind>(k);
            return true;
        }
    return false;
}

WeatherSystem::Targets WeatherSystem::targetsFor(WeatherKind kind)
{
    switch (kind)
    {
        case WeatherKind::Clear: return {0.08f, 0.35f, 0.03f, 0.0f, 1.5f};
        case WeatherKind::Cloudy: return {0.42f, 0.5f, 0.06f, 0.0f, 3.0f};
        case WeatherKind::Overcast: return {0.92f, 0.7f, 0.18f, 0.0f, 4.0f};
        case WeatherKind::Rain: return {0.97f, 0.85f, 0.35f, 0.6f, 6.0f};
        case WeatherKind::Storm: return {1.0f, 0.95f, 0.45f, 1.0f, 11.0f};
        case WeatherKind::Snow: return {0.95f, 0.65f, 0.40f, 0.55f, 3.5f};
        case WeatherKind::Hail: return {1.0f, 0.95f, 0.40f, 0.9f, 9.0f};
    }
    return {0.3f, 0.5f, 0.05f, 0.0f, 2.0f};
}

void WeatherSystem::setKind(WeatherKind kind, bool immediate)
{
    target_ = kind;
    state_.kind = kind;
    dwellHours_ = 0.3f + random() * 1.2f;
    if (immediate)
    {
        const Targets t = targetsFor(kind);
        state_.cloudCoverage = t.coverage;
        state_.cloudDensity = t.density;
        state_.haze = t.haze;
        state_.precipitation = t.precipitation;
        state_.windSpeed = t.wind;
        if (kind == WeatherKind::Rain || kind == WeatherKind::Storm || kind == WeatherKind::Hail) state_.wetness = 1.0f;
        if (kind == WeatherKind::Snow)
        {
            state_.snowCover = 0.6f;
            if (std::isnan(temperatureOverride_)) temperatureOverride_ = -3.0f;
        }
    }
}

void WeatherSystem::cycleKind()
{
    const int next = (static_cast<int>(target_) + 1) % (static_cast<int>(WeatherKind::Hail) + 1);
    setKind(static_cast<WeatherKind>(next), false);
    if (static_cast<WeatherKind>(next) == WeatherKind::Snow && std::isnan(temperatureOverride_))
        temperatureOverride_ = -3.0f;
    else if (static_cast<WeatherKind>(next) != WeatherKind::Snow && temperatureOverride_ == -3.0f)
        temperatureOverride_ = std::nanf("");
}

WeatherKind WeatherSystem::pickNext()
{
    const float r = random();
    const bool cold = state_.temperatureC < 1.0f;
    switch (target_)
    {
        case WeatherKind::Clear: return r < 0.6f ? WeatherKind::Cloudy : WeatherKind::Clear;
        case WeatherKind::Cloudy:
            if (r < 0.35f) return WeatherKind::Clear;
            if (r < 0.7f) return WeatherKind::Overcast;
            return cold ? WeatherKind::Snow : WeatherKind::Rain;
        case WeatherKind::Overcast:
            if (r < 0.35f) return WeatherKind::Cloudy;
            if (r < 0.8f) return cold ? WeatherKind::Snow : WeatherKind::Rain;
            return WeatherKind::Overcast;
        case WeatherKind::Rain:
            if (r < 0.3f) return WeatherKind::Overcast;
            if (r < 0.55f) return WeatherKind::Storm;
            if (r < 0.65f) return WeatherKind::Hail;
            return WeatherKind::Rain;
        case WeatherKind::Storm:
            if (r < 0.5f) return WeatherKind::Rain;
            if (r < 0.7f) return WeatherKind::Hail;
            return WeatherKind::Storm;
        case WeatherKind::Snow:
            if (r < 0.4f) return WeatherKind::Overcast;
            return WeatherKind::Snow;
        case WeatherKind::Hail: return r < 0.6f ? WeatherKind::Rain : WeatherKind::Storm;
    }
    return WeatherKind::Cloudy;
}

void WeatherSystem::advance(float gameHours, float realSeconds, float hourOfDay)
{
    gameHours = std::max(0.0f, gameHours) * speed_;
    realSeconds = std::max(0.0f, realSeconds);

    // State machine.
    if (!hold_)
    {
        dwellHours_ -= gameHours;
        if (dwellHours_ <= 0.0f)
        {
            const WeatherKind next = pickNext();
            target_ = next;
            state_.kind = next;
            dwellHours_ = 0.3f + random() * 1.2f;
        }
    }

    // Temperature: September mean with a diurnal swing, colder under cloud.
    const float diurnal = 4.0f * std::cos((hourOfDay - 15.0f) / 24.0f * 2.0f * kPi);   // warmest at 15:00, coldest at 03:00
    const float model = 14.0f + diurnal - 6.0f * state_.cloudCoverage * state_.cloudDensity;
    state_.temperatureC = std::isnan(temperatureOverride_) ? model : temperatureOverride_;

    // Glide the continuous values towards the kind's targets (rates per game hour).
    const Targets t = targetsFor(target_);
    state_.cloudCoverage = towards(state_.cloudCoverage, t.coverage, 1.2f * gameHours);
    state_.cloudDensity = towards(state_.cloudDensity, t.density, 0.8f * gameHours);
    state_.haze = towards(state_.haze, t.haze, 0.6f * gameHours);
    state_.windSpeed = towards(state_.windSpeed, t.wind, 12.0f * gameHours);
    state_.windDirection += (random() - 0.5f) * 0.4f * gameHours;
    // Rain needs cloud first: precipitation only rises once the cover is nearly there.
    const float gate = std::clamp((state_.cloudCoverage - 0.75f) / 0.15f, 0.0f, 1.0f);
    state_.precipitation = towards(state_.precipitation, t.precipitation * gate, 2.5f * gameHours);

    // Hail: short bursts inside a storm or hail state.
    if (target_ == WeatherKind::Hail || target_ == WeatherKind::Storm)
    {
        hailBurstHours_ -= gameHours;
        if (hailBurstHours_ < -0.15f) hailBurstHours_ = (target_ == WeatherKind::Hail ? 0.12f : 0.05f) + random() * 0.1f;
    }
    else
    {
        hailBurstHours_ = -1.0f;
    }
    const bool hailing = hailBurstHours_ > 0.0f && state_.precipitation > 0.05f;
    const bool cold = state_.temperatureC < 1.0f;
    state_.type = state_.precipitation <= 0.01f ? PrecipitationType::None
                  : hailing                     ? PrecipitationType::Hail
                  : cold                        ? PrecipitationType::Snow
                                                : PrecipitationType::Rain;

    // Surfaces: rain soaks in ~10 game minutes and dries in an hour or two;
    // snow settles when it snows and melts above 2 °C.
    const bool raining = state_.type == PrecipitationType::Rain || state_.type == PrecipitationType::Hail;
    if (raining) state_.wetness = towards(state_.wetness, 1.0f, state_.precipitation * 6.0f * gameHours);
    else state_.wetness = towards(state_.wetness, 0.0f, (state_.temperatureC > 18.0f ? 1.0f : 0.5f) * gameHours);
    if (state_.type == PrecipitationType::Snow) state_.snowCover = towards(state_.snowCover, 1.0f, state_.precipitation * 2.0f * gameHours);
    else if (state_.temperatureC > 2.0f) state_.snowCover = towards(state_.snowCover, 0.0f, 1.0f * gameHours);
    if (state_.snowCover > 0.3f) state_.wetness = std::max(state_.wetness, 0.3f);

    // Lightning in real time during storms: a leader flash and a brighter return stroke.
    state_.lightning = 0.0f;
    if (target_ == WeatherKind::Storm && state_.cloudCoverage > 0.9f)
    {
        nextFlashSeconds_ -= realSeconds;
        if (nextFlashSeconds_ <= 0.0f)
        {
            nextFlashSeconds_ = 3.0f + random() * 12.0f;
            flashAge_ = 0.0f;
            flashStrength_ = 0.5f + random() * 0.5f;
            state_.lightningAzimuth = random() * 2.0f * kPi;
        }
    }
    flashAge_ += realSeconds;
    if (flashAge_ < 0.6f)
    {
        const float a = flashAge_;
        const float first = std::exp(-a * 18.0f);
        const float second = a > 0.12f ? std::exp(-(a - 0.12f) * 9.0f) * 1.3f : 0.0f;
        state_.lightning = std::min(1.0f, flashStrength_ * (first + second));
    }
    state_.starVisibility = std::clamp(1.0f - state_.cloudCoverage * 1.1f - state_.haze * 0.5f, 0.0f, 1.0f);
}

std::string WeatherSystem::describe() const
{
    char buffer[160];
    const char* falling = state_.type == PrecipitationType::Rain ? "rain"
                          : state_.type == PrecipitationType::Snow ? "snow"
                          : state_.type == PrecipitationType::Hail ? "hail" : "dry";
    std::snprintf(buffer, sizeof(buffer), "%s (cover %.2f, %s %.2f, wet %.2f, snow %.2f, wind %.1f m/s, %.1f C)",
                  name(target_), static_cast<double>(state_.cloudCoverage), falling, static_cast<double>(state_.precipitation),
                  static_cast<double>(state_.wetness), static_cast<double>(state_.snowCover),
                  static_cast<double>(state_.windSpeed), static_cast<double>(state_.temperatureC));
    return buffer;
}

float chimneySmokeLevel(float temperatureC)
{
    return std::clamp((14.0f - temperatureC) / 6.0f, 0.0f, 1.0f);
}

}  // namespace CnaRoom
