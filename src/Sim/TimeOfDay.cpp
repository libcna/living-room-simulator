// SPDX-License-Identifier: MIT
#include "CnaRoom/Sim/TimeOfDay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using Microsoft::Xna::Framework::Vector3;

namespace CnaRoom {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kDegrees = kPi / 180.0f;
constexpr float kSynodicMonthDays = 29.530589f;

float wrap24(float hours)
{
    hours = std::fmod(hours, 24.0f);
    return hours < 0.0f ? hours + 24.0f : hours;
}
}  // namespace

void TimeOfDay::setHours(float hours) { hours_ = wrap24(hours); }

void TimeOfDay::advance(float realSeconds)
{
    if (paused_ || secondsPerDay_ <= 0.0f || realSeconds <= 0.0f) return;
    hours_ = wrap24(hours_ + realSeconds * (24.0f / secondsPerDay_));
}

float TimeOfDay::solarDeclinationDegrees() const
{
    return 23.44f * std::sin(2.0f * kPi * (static_cast<float>(dayOfYear_) - 81.0f) / 365.0f);
}

Vector3 TimeOfDay::direction(float hourAngleDegrees, float declinationDegrees) const
{
    const float phi = latitudeDegrees_ * kDegrees;
    const float delta = declinationDegrees * kDegrees;
    const float h = hourAngleDegrees * kDegrees;
    const float sinEl = std::sin(phi) * std::sin(delta) + std::cos(phi) * std::cos(delta) * std::cos(h);
    const float el = std::asin(std::fmax(-1.0f, std::fmin(1.0f, sinEl)));
    // Azimuth clockwise from north; 180 at local noon in the northern hemisphere.
    const float az = std::atan2(std::sin(h), std::cos(h) * std::sin(phi) - std::tan(delta) * std::cos(phi)) + kPi;
    return Vector3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
}

Vector3 TimeOfDay::sunDirection() const
{
    return direction(15.0f * (hours_ - 12.0f), solarDeclinationDegrees());
}

Vector3 TimeOfDay::moonDirection() const
{
    // The moon lags the sun by its age as a fraction of the synodic month;
    // its declination is approximated by the sun's, mirrored at full moon.
    const float lagDegrees = 360.0f * (moonAgeDays_ / kSynodicMonthDays);
    const float declination = -solarDeclinationDegrees() * std::cos(2.0f * kPi * moonAgeDays_ / kSynodicMonthDays) * 0.8f;
    return direction(15.0f * (hours_ - 12.0f) - lagDegrees, declination);
}

float TimeOfDay::sunElevationDegrees() const
{
    return std::asin(std::fmax(-1.0f, std::fmin(1.0f, sunDirection().Y))) / kDegrees;
}

float TimeOfDay::sunAzimuthDegrees() const
{
    const Vector3 d = sunDirection();
    float az = std::atan2(d.X, d.Z) / kDegrees;
    if (az < 0.0f) az += 360.0f;
    return az;
}

float TimeOfDay::moonPhase() const
{
    return 0.5f * (1.0f - std::cos(2.0f * kPi * moonAgeDays_ / kSynodicMonthDays));
}

std::string TimeOfDay::clockText() const
{
    const int minutes = static_cast<int>(std::lround(hours_ * 60.0f)) % (24 * 60);
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes / 60, minutes % 60);
    return buffer;
}

bool TimeOfDay::parseClock(const std::string& text, float& hours)
{
    int h = 0, m = 0;
    if (std::sscanf(text.c_str(), "%d:%d", &h, &m) == 2)
    {
        if (h < 0 || h > 24 || m < 0 || m > 59) return false;
        hours = static_cast<float>(h) + static_cast<float>(m) / 60.0f;
        return true;
    }
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || value < 0.0f || value > 24.0f) return false;
    hours = value;
    return true;
}

int bakeFacesPerFrame(std::size_t total, float gameMinutesPerFrame, float withinMinutes)
{
    if (total == 0 || gameMinutesPerFrame <= 0.0f || withinMinutes <= 0.0f) return 1;
    const float wanted = std::ceil(static_cast<float>(total) * gameMinutesPerFrame / withinMinutes);
    const int cap = std::max(1, static_cast<int>(total / 4));
    return std::clamp(static_cast<int>(std::min(wanted, 1.0e6f)), 1, cap);
}

}  // namespace CnaRoom
