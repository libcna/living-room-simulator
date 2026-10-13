// SPDX-License-Identifier: MIT
// Checks on the exposure meter's CPU half and the auto exposure's adaptation,
// neither of which needs a graphics device: the cell statistics, the RGBA8
// log2 codec the GPU pass writes, and the exponential approach to a target.
#include "CnaRoom/Effects/AutoExposureEXT.hpp"
#include "CnaRoom/Render/ExposureStatistics.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

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
bool nearRelative(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance * std::fabs(b); }

using CnaRoom::ExposureStatistics;
constexpr int kW = ExposureStatistics::kCellsX;
constexpr int kH = ExposureStatistics::kCellsY;

std::vector<float> field(float value) { return std::vector<float>(static_cast<std::size_t>(kW * kH), value); }

void testStatistics()
{
    // A uniform field: every mean is that value, nothing is a highlight.
    {
        const auto reading = ExposureStatistics::evaluate(field(0.25f), kW, kH);
        check(reading.valid(), "a uniform field gives a valid reading");
        check(nearRelative(reading.plain, 0.25f, 1e-4f), "uniform field: plain mean is the value");
        check(nearRelative(reading.weighted, 0.25f, 1e-4f), "uniform field: weighted mean is the value");
        check(reading.highlightShare == 0.0f, "uniform field: no highlights");
    }
    // A brighter field reads brighter than a darker one, in both statistics.
    {
        const auto dark = ExposureStatistics::evaluate(field(0.001f), kW, kH);
        const auto bright = ExposureStatistics::evaluate(field(0.5f), kW, kH);
        check(bright.plain > dark.plain, "brighter field: larger plain reading");
        check(bright.weighted > dark.weighted, "brighter field: larger weighted reading");
    }
    // A small, very bright patch (a lamp shade) is down-weighted: the weighted
    // mean stays near the room, the plain mean is pulled up, and the share of
    // rejected weight is reported.
    {
        std::vector<float> cells = field(0.02f);
        for (int y = 10; y < 16; ++y)
            for (int x = 28; x < 36; ++x) cells[static_cast<std::size_t>(y * kW + x)] = 40.0f;
        const auto reading = ExposureStatistics::evaluate(cells, kW, kH);
        const auto room = ExposureStatistics::evaluate(field(0.02f), kW, kH);
        check(reading.highlightShare > 0.0f, "bright patch: some weight was rejected");
        check(reading.highlightShare < 0.2f, "bright patch: most weight remains");
        check(reading.weighted < reading.plain, "bright patch: weighted reading below the plain one");
        check(reading.weighted < room.weighted * 1.6f, "bright patch: weighted reading stays near the room");
        // 48 of 2304 cells at 2000x the room: a geometric mean moves by (2000)^(48/2304) = 1.17x.
        check(reading.plain > room.plain * 1.1f, "bright patch: plain reading is pulled up");
    }
    // Centre weighting: the same bright block counts more in the middle than in a corner.
    {
        std::vector<float> centre = field(0.02f), corner = field(0.02f);
        for (int y = 15; y < 21; ++y)
            for (int x = 29; x < 35; ++x) centre[static_cast<std::size_t>(y * kW + x)] = 0.1f;
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x) corner[static_cast<std::size_t>(y * kW + x)] = 0.1f;
        const auto c = ExposureStatistics::evaluate(centre, kW, kH);
        const auto k = ExposureStatistics::evaluate(corner, kW, kH);
        check(c.weighted > k.weighted, "centre weighting: a central block weighs more than a corner one");
        check(near(c.plain, k.plain, 1e-6f), "centre weighting: the plain mean does not care where the block is");
    }
    // Non-finite and negative cells count as black: outputs are finite and at the floor.
    {
        std::vector<float> cells = field(std::numeric_limits<float>::quiet_NaN());
        cells[0] = std::numeric_limits<float>::infinity();
        cells[1] = -std::numeric_limits<float>::infinity();
        cells[2] = -5.0f;
        const auto reading = ExposureStatistics::evaluate(cells, kW, kH);
        check(std::isfinite(reading.plain) && std::isfinite(reading.weighted) && std::isfinite(reading.highlightShare),
              "non-finite cells: finite outputs");
        check(nearRelative(reading.plain, ExposureStatistics::kFloor, 1e-4f), "non-finite cells: plain reading at the floor");
        check(nearRelative(reading.weighted, ExposureStatistics::kFloor, 1e-4f), "non-finite cells: weighted reading at the floor");
        check(reading.valid(), "non-finite cells: still a reading (black), not an error");
        // One NaN among ordinary cells does not poison the rest.
        std::vector<float> mostly = field(0.1f);
        mostly[100] = std::numeric_limits<float>::quiet_NaN();
        const auto one = ExposureStatistics::evaluate(mostly, kW, kH);
        check(std::isfinite(one.plain) && one.plain > 0.05f && one.plain < 0.1f, "one NaN cell counts as black, nothing more");
    }
    // Bad grids are refused rather than read past the end.
    {
        bool threw = false;
        try { (void)ExposureStatistics::evaluate(field(1.0f), kW, kH + 1); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw, "a cell array that does not match its grid is refused");
    }
}

void testCodec()
{
    // Round trip across the whole range, against the reference encoder: the
    // 24-bit code resolves 48 stops to 2.9e-6 of a stop.
    for (float lum : {1e-6f, 1e-5f, 1e-4f, 0.0006f, 0.0286f, 0.075f, 0.18f, 1.0f, 3.6f, 42.7f, 1000.0f, 65535.0f, 1.0e6f})
    {
        const auto bytes = ExposureStatistics::encodeLuminance(lum);
        const float back = ExposureStatistics::decodeLuminance(bytes[0], bytes[1], bytes[2]);
        check(nearRelative(back, lum, 1e-4f), "codec round trip at " + std::to_string(lum));
    }
    // The ends of the range clamp; black and not-a-number are code zero and decode to 0.
    {
        const auto top = ExposureStatistics::encodeLuminance(1.0e12f);
        check(top[0] == 255 && top[1] == 255 && top[2] == 255, "luminance beyond the range clamps to the top code");
        check(nearRelative(ExposureStatistics::decodeLuminance(255, 255, 255), std::exp2(ExposureStatistics::kLog2Max), 1e-4f),
              "the top code decodes to 2^24");
        const auto zero = ExposureStatistics::encodeLuminance(0.0f);
        check(zero == std::array<std::uint8_t, 3>{0, 0, 0}, "black is code zero");
        const auto nan = ExposureStatistics::encodeLuminance(std::numeric_limits<float>::quiet_NaN());
        check(nan == std::array<std::uint8_t, 3>{0, 0, 0}, "not-a-number is code zero");
        const auto inf = ExposureStatistics::encodeLuminance(std::numeric_limits<float>::infinity());
        check(inf == std::array<std::uint8_t, 3>{0, 0, 0}, "infinity is code zero");
        const auto negative = ExposureStatistics::encodeLuminance(-1.0f);
        check(negative == std::array<std::uint8_t, 3>{0, 0, 0}, "a negative luminance is code zero");
        check(ExposureStatistics::decodeLuminance(0, 0, 0) == 0.0f, "code zero decodes to 0");
        const auto tiny = ExposureStatistics::encodeLuminance(1.0e-9f);
        check(tiny == std::array<std::uint8_t, 3>{0, 0, 0}, "luminance below the range clamps to code zero");
    }
    // The code is monotonic in the luminance (the GPU pass and the CPU agree on ordering).
    {
        std::uint32_t previous = 0;
        for (int stop = -30; stop <= 30; ++stop)
        {
            const auto b = ExposureStatistics::encodeLuminance(std::exp2(static_cast<float>(stop)));
            const std::uint32_t code = (static_cast<std::uint32_t>(b[0]) << 16) | (static_cast<std::uint32_t>(b[1]) << 8) | b[2];
            check(code >= previous, "codec monotonic at stop " + std::to_string(stop));
            previous = code;
        }
    }
}

void testAutoExposure()
{
    using CnaRoom::Effects::AutoExposureEXT;
    const auto make = [] {
        AutoExposureEXT exposure;
        exposure.setKeyValue(0.05f);
        exposure.setExposureRange(0.3f, 2000.0f);
        exposure.setAdaptationSpeeds(2.5f, 1.2f);
        return exposure;
    };
    // dt <= 0 snaps straight to the target key / luminance.
    {
        AutoExposureEXT exposure = make();
        check(near(exposure.update(0.025f, 0.0f), 2.0f, 1e-5f), "dt = 0 snaps to key / luminance");
        check(near(exposure.update(0.0005f, -1.0f), 100.0f, 1e-3f), "dt < 0 snaps too");
        check(near(exposure.getExposure(), 100.0f, 1e-3f), "getExposure reports the snapped value");
    }
    // The exposure moves toward its target monotonically and settles there.
    {
        AutoExposureEXT exposure = make();
        exposure.setExposure(1.0f);
        float previous = exposure.getExposure();
        bool monotone = true;
        for (int i = 0; i < 600; ++i)
        {
            const float now = exposure.update(0.0005f, 1.0f / 60.0f);   // target 100
            if (now < previous - 1e-6f) monotone = false;
            previous = now;
        }
        check(monotone, "darkening: the exposure rises monotonically toward the target");
        check(near(previous, 100.0f, 0.5f), "darkening: the exposure settles at the target");
        previous = exposure.getExposure();
        monotone = true;
        for (int i = 0; i < 600; ++i)
        {
            const float now = exposure.update(0.05f, 1.0f / 60.0f);   // target 1
            if (now > previous + 1e-6f) monotone = false;
            previous = now;
        }
        check(monotone, "brightening: the exposure falls monotonically toward the target");
        check(near(previous, 1.0f, 0.01f), "brightening: the exposure settles at the target");
    }
    // It never leaves [min, max], whatever the luminance says.
    {
        AutoExposureEXT exposure = make();
        check(near(exposure.update(1.0e6f, 0.0f), 0.3f, 1e-6f), "a blinding scene asks for the minimum exposure");
        // The controller floors the luminance at 1e-4 before dividing, so black asks for key / 1e-4.
        check(near(exposure.update(1.0e-9f, 0.0f), 500.0f, 1e-2f), "a black scene asks for key / 1e-4");
        check(near(exposure.update(0.0f, 0.0f), 500.0f, 1e-2f), "a zero reading asks for key / 1e-4");
        AutoExposureEXT wide = make();
        wide.setKeyValue(1.0f);
        check(near(wide.update(0.0f, 0.0f), 2000.0f, 1e-3f), "a black scene is clamped to the maximum exposure");
        exposure.setExposure(1.0f);
        for (int i = 0; i < 100; ++i)
        {
            const float e = exposure.update(i % 2 == 0 ? 1.0e6f : 0.0f, 0.5f);
            check(e >= 0.3f && e <= 2000.0f, "exposure stays within [min, max] at step " + std::to_string(i));
        }
        check(std::isfinite(exposure.update(std::numeric_limits<float>::quiet_NaN(), 0.1f)), "a NaN reading keeps the exposure finite");
        check(std::isfinite(exposure.update(std::numeric_limits<float>::infinity(), 0.1f)), "an infinite reading keeps the exposure finite");
        check(exposure.getExposure() >= 0.3f && exposure.getExposure() <= 2000.0f, "a non-finite reading keeps the exposure in range");
    }
    // Brightening (the scene got brighter, the exposure comes down) is the fast direction.
    {
        AutoExposureEXT down = make(), up = make();
        down.setExposure(100.0f);
        up.setExposure(1.0f);
        const float afterDown = down.update(0.05f, 0.25f);   // target 1: two decades down
        const float afterUp = up.update(0.0005f, 0.25f);     // target 100: two decades up
        const float downFraction = (100.0f - afterDown) / 99.0f;
        const float upFraction = (afterUp - 1.0f) / 99.0f;
        check(downFraction > upFraction, "brightening adapts faster than darkening");
        check(near(downFraction, 1.0f - std::exp(-2.5f * 0.25f), 1e-4f), "brightening uses the brightening speed");
        check(near(upFraction, 1.0f - std::exp(-1.2f * 0.25f), 1e-4f), "darkening uses the darkening speed");
    }
    // Setters validate their arguments.
    {
        AutoExposureEXT exposure = make();
        bool threw = false;
        try { exposure.setKeyValue(0.0f); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "a zero key value is refused");
        threw = false;
        try { exposure.setExposureRange(2.0f, 1.0f); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "an inverted exposure range is refused");
        threw = false;
        try { exposure.setAdaptationSpeeds(1.0f, -1.0f); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "a negative adaptation speed is refused");
        threw = false;
        try { exposure.setExposure(-1.0f); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "a negative exposure is refused");
        exposure.setExposure(5000.0f);
        check(near(exposure.getExposure(), 2000.0f, 1e-3f), "setExposure clamps to the range");
    }
}

}  // namespace

int main()
{
    testStatistics();
    testCodec();
    testAutoExposure();
    if (failures == 0) std::printf("exposure tests: ok\n");
    return failures == 0 ? 0 : 1;
}
