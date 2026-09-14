// SPDX-License-Identifier: MIT
// Checks on the simulation classes that need no graphics device: the solar
// clock's geometry and the weather state machine's dynamics.
#include "CnaRoom/Sim/TimeOfDay.hpp"
#include "CnaRoom/Sim/WeatherSystem.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition)
    {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

void testTimeOfDay()
{
    using CnaRoom::TimeOfDay;
    TimeOfDay clock;   // 50.1 N, day 256
    clock.setHours(12.0f);
    check(near(clock.sunAzimuthDegrees(), 180.0f, 0.5f), "noon sun is due south");
    // Elevation at solar noon = 90 - latitude + declination.
    const float expected = 90.0f - 50.1f + clock.solarDeclinationDegrees();
    check(near(clock.sunElevationDegrees(), expected, 0.2f), "noon elevation from the declination");
    check(clock.sunDirection().Z < -0.5f, "the noon sun stands beyond the window wall (-Z is south)");

    // Sunrise between 06:00 and 07:00, sunset between 18:00 and 19:00 in mid September.
    float sunrise = -1.0f, sunset = -1.0f;
    for (int minute = 0; minute < 24 * 60; ++minute)
    {
        clock.setHours(static_cast<float>(minute) / 60.0f);
        const float now = clock.sunElevationDegrees();
        clock.setHours(static_cast<float>(minute + 1) / 60.0f);
        const float next = clock.sunElevationDegrees();
        if (now < 0.0f && next >= 0.0f) sunrise = static_cast<float>(minute) / 60.0f;
        if (now >= 0.0f && next < 0.0f) sunset = static_cast<float>(minute) / 60.0f;
    }
    // Solar time: mid-September sunrise at 50 N is ~05:45 (civil clocks add the zone and DST).
    check(sunrise > 5.5f && sunrise < 6.5f, "sunrise near a quarter to six solar time, got " + std::to_string(sunrise));
    check(sunset > 18.0f && sunset < 19.0f, "sunset in the eighteen o'clock hour, got " + std::to_string(sunset));
    clock.setHours(0.0f);
    check(clock.sunElevationDegrees() < -30.0f, "midnight sun far below the horizon");

    // Sunrise is in the east: azimuth < 180 in the morning, > 180 in the afternoon.
    clock.setHours(9.0f);
    check(clock.sunAzimuthDegrees() < 180.0f, "morning sun in the east");
    clock.setHours(15.0f);
    check(clock.sunAzimuthDegrees() > 180.0f, "afternoon sun in the west");

    // The clock advances at the configured speed and wraps.
    clock.setSecondsPerDay(60.0f);
    clock.setHours(23.5f);
    clock.advance(2.5f);   // one game hour
    check(near(clock.hours(), 0.5f, 1e-3f), "advance wraps past midnight");
    clock.setPaused(true);
    clock.advance(10.0f);
    check(near(clock.hours(), 0.5f, 1e-3f), "a paused clock stands still");

    // Moon: full at half a synodic month, new at zero; the full moon is opposite the sun.
    clock.setMoonAgeDays(14.765f);
    check(near(clock.moonPhase(), 1.0f, 0.01f), "full moon phase");
    clock.setHours(0.0f);
    check(clock.moonDirection().Y > 0.3f, "a full moon is up at midnight");
    clock.setMoonAgeDays(0.0f);
    check(near(clock.moonPhase(), 0.0f, 0.01f), "new moon phase");

    float hours = 0.0f;
    check(TimeOfDay::parseClock("21:30", hours) && near(hours, 21.5f, 1e-4f), "parse HH:MM");
    check(TimeOfDay::parseClock("6.25", hours) && near(hours, 6.25f, 1e-4f), "parse decimal hours");
    check(!TimeOfDay::parseClock("25:00", hours), "reject 25:00");
    check(!TimeOfDay::parseClock("noon", hours), "reject words");
    clock.setHours(9.0f + 7.0f / 60.0f);
    check(clock.clockText() == "09:07", "clock text, got " + clock.clockText());
}

void testWeather()
{
    using CnaRoom::PrecipitationType;
    using CnaRoom::WeatherKind;
    using CnaRoom::WeatherSystem;

    WeatherKind kind = WeatherKind::Clear;
    check(WeatherSystem::parse("storm", kind) && kind == WeatherKind::Storm, "parse storm");
    check(!WeatherSystem::parse("drizzle", kind), "reject unknown kinds");

    // Held rain: wet, raining, no lightning.
    WeatherSystem rain(3u);
    rain.setKind(WeatherKind::Rain, true);
    rain.setHold(true);
    for (int i = 0; i < 60; ++i) rain.advance(1.0f / 60.0f, 1.0f / 60.0f, 15.0f);
    check(rain.state().type == PrecipitationType::Rain, "held rain rains");
    check(rain.state().wetness > 0.95f, "rain soaks the ground");
    check(rain.state().lightning == 0.0f, "no lightning in plain rain");
    check(rain.state().kind == WeatherKind::Rain, "held kind does not change");
    // The diurnal temperature: mid-afternoon is the day's warmest, the small hours the coldest
    // (a sign slip once had it the other way round, and the stove is lit by it).
    WeatherSystem warm(3u), cold(3u);
    warm.setHold(true);
    cold.setHold(true);
    warm.advance(0.0f, 0.0f, 15.0f);
    cold.advance(0.0f, 0.0f, 3.0f);
    check(warm.state().temperatureC > cold.state().temperatureC + 6.0f, "15:00 is the warmest hour, 03:00 the coldest");
    WeatherSystem evening(3u);
    evening.setHold(true);
    evening.advance(0.0f, 0.0f, 22.0f);
    check(evening.state().temperatureC < 14.0f && evening.state().temperatureC > 9.0f, "a clear evening sits in the low teens");

    // Cold turns the same rain into snow that settles.
    rain.setTemperatureOverride(-4.0f);
    for (int i = 0; i < 120; ++i) rain.advance(1.0f / 60.0f, 1.0f / 60.0f, 15.0f);
    check(rain.state().type == PrecipitationType::Snow, "rain below freezing is snow");
    check(rain.state().snowCover > 0.5f, "snow settles, cover " + std::to_string(rain.state().snowCover));

    // Clearing: coverage glides down monotonically and precipitation stops.
    WeatherSystem clearing(5u);
    clearing.setKind(WeatherKind::Overcast, true);
    clearing.setHold(true);
    clearing.setKind(WeatherKind::Clear, false);
    clearing.setHold(true);
    float previous = clearing.state().cloudCoverage;
    bool monotonic = true;
    for (int i = 0; i < 240; ++i)
    {
        clearing.advance(1.0f / 60.0f, 1.0f / 60.0f, 12.0f);
        if (clearing.state().cloudCoverage > previous + 1e-5f) monotonic = false;
        previous = clearing.state().cloudCoverage;
    }
    check(monotonic, "coverage only falls while clearing");
    check(clearing.state().cloudCoverage < 0.15f, "clear sky reached, cover " + std::to_string(previous));
    check(clearing.state().type == PrecipitationType::None, "nothing falls from a clear sky");

    // A storm flashes within twenty real seconds and the flash decays.
    WeatherSystem storm(11u);
    storm.setKind(WeatherKind::Storm, true);
    storm.setHold(true);
    float peak = 0.0f;
    for (int i = 0; i < 20 * 60; ++i)
    {
        storm.advance(0.0f, 1.0f / 60.0f, 22.0f);
        peak = std::max(peak, storm.state().lightning);
    }
    check(peak > 0.3f, "a storm produces lightning, peak " + std::to_string(peak));
    for (int i = 0; i < 60; ++i) storm.advance(0.0f, 1.0f / 60.0f, 22.0f);   // flashes last < 1 s
    bool decayed = true;
    for (int i = 0; i < 5 && decayed; ++i)
    {
        storm.advance(0.0f, 0.0f, 22.0f);
        decayed = storm.state().lightning <= 1.0f;
    }
    check(decayed, "lightning stays in range");

    // Free-running weather changes kind eventually and never leaves its ranges.
    WeatherSystem free(21u);
    bool changed = false;
    bool inRange = true;
    for (int i = 0; i < 24 * 60; ++i)   // a day in game minutes
    {
        free.advance(1.0f / 60.0f, 1.0f / 60.0f, static_cast<float>(i) / 60.0f);
        const auto& s = free.state();
        if (s.kind != WeatherKind::Cloudy) changed = true;
        inRange = inRange && s.cloudCoverage >= 0.0f && s.cloudCoverage <= 1.0f && s.precipitation >= 0.0f
                  && s.precipitation <= 1.0f && s.wetness >= 0.0f && s.wetness <= 1.0f && s.snowCover >= 0.0f
                  && s.snowCover <= 1.0f && s.windSpeed >= 0.0f && s.starVisibility >= 0.0f && s.starVisibility <= 1.0f;
    }
    check(changed, "free-running weather leaves its first kind within a day");
    check(inRange, "weather values stay in range");
    check(!free.describe().empty(), "describe() says something");
}

}  // namespace

int main()
{
    testTimeOfDay();
    testWeather();
    if (failures == 0) std::printf("cna_room_sim_tests: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
