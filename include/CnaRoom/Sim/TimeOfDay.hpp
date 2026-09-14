// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <string>

namespace CnaRoom {

/**
 * @brief A slow, continuous solar clock.
 *
 * Solar time at a given latitude and day of the year: the sun's elevation
 * and azimuth follow the standard declination / hour-angle formulas (the
 * equation of time is ignored: solar noon is 12:00). The moon is placed by
 * its age since new moon, lagging the sun by that fraction of a day.
 *
 * Scene axes: +Z is north, +X is east, so an azimuth `a` clockwise from north
 * maps to the direction (sin a, ., cos a). The room's windows face -Z (south).
 */
class TimeOfDay
{
public:
    void setHours(float hours);
    [[nodiscard]] float hours() const { return hours_; }
    void setLatitudeDegrees(float degrees) { latitudeDegrees_ = degrees; }
    [[nodiscard]] float latitudeDegrees() const { return latitudeDegrees_; }
    void setDayOfYear(int day) { dayOfYear_ = day; }
    [[nodiscard]] int dayOfYear() const { return dayOfYear_; }
    /// Real seconds per 24 h of game time; 0 freezes the clock.
    void setSecondsPerDay(float seconds) { secondsPerDay_ = seconds; }
    [[nodiscard]] float secondsPerDay() const { return secondsPerDay_; }
    void setPaused(bool paused) { paused_ = paused; }
    [[nodiscard]] bool paused() const { return paused_; }
    /// Days since new moon (0..29.53).
    void setMoonAgeDays(float days) { moonAgeDays_ = days; }
    [[nodiscard]] float moonAgeDays() const { return moonAgeDays_; }

    /// Advances by `realSeconds` of wall-clock time.
    void advance(float realSeconds);
    /// Jumps by a number of game hours (wraps).
    void jump(float hours) { setHours(hours_ + hours); }

    [[nodiscard]] Microsoft::Xna::Framework::Vector3 sunDirection() const;   ///< towards the sun
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 moonDirection() const;  ///< towards the moon
    [[nodiscard]] float sunElevationDegrees() const;
    [[nodiscard]] float sunAzimuthDegrees() const;
    /// Illuminated fraction 0..1 from the age.
    [[nodiscard]] float moonPhase() const;
    [[nodiscard]] float solarDeclinationDegrees() const;
    /// "HH:MM" of the game clock.
    [[nodiscard]] std::string clockText() const;
    /// Parses "HH:MM" or "HH.5"; returns false when malformed.
    static bool parseClock(const std::string& text, float& hours);

private:
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 direction(float hourAngleDegrees, float declinationDegrees) const;

    float hours_ = 14.5f;
    float latitudeDegrees_ = 50.1f;   // Prague
    int dayOfYear_ = 256;             // 13 September
    float secondsPerDay_ = 24.0f * 60.0f;   // one game day in 24 real minutes
    bool paused_ = false;
    float moonAgeDays_ = 10.0f;       // waxing gibbous
};

}  // namespace CnaRoom
