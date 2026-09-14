// SPDX-License-Identifier: MIT
// CPU-only checks on the procedural texture bakers: value ranges, tiling,
// alpha coverage and the mip builder's coverage preservation.
#include "CnaRoom/Assets/Image.hpp"
#include "CnaRoom/Assets/TextureBaker.hpp"

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

float meanChannel(const CnaRoom::Assets::Image& image, int channel)
{
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = static_cast<std::size_t>(channel); i < image.rgba.size(); i += 4, ++n) sum += image.rgba[i];
    return n == 0 ? 0.0f : static_cast<float>(sum / static_cast<double>(n) / 255.0);
}

/// Mean of one channel over the rows between two fractions of the height.
float meanChannelRows(const CnaRoom::Assets::Image& image, int channel, float top, float bottom)
{
    double sum = 0.0;
    std::size_t n = 0;
    const int y0 = static_cast<int>(top * static_cast<float>(image.height)), y1 = static_cast<int>(bottom * static_cast<float>(image.height));
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < image.width; ++x, ++n)
            sum += image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(channel)];
    return n == 0 ? 0.0f : static_cast<float>(sum / static_cast<double>(n) / 255.0);
}

float coverage(const CnaRoom::Assets::Image& image)
{
    std::size_t above = 0, n = 0;
    for (std::size_t i = 3; i < image.rgba.size(); i += 4, ++n)
        if (image.rgba[i] >= 128) ++above;
    return n == 0 ? 0.0f : static_cast<float>(above) / static_cast<float>(n);
}

bool tilesHorizontally(const CnaRoom::Assets::Image& image, int tolerance)
{
    // The first and last columns must be neighbours-alike for a seamless wrap.
    int worst = 0;
    for (int y = 0; y < image.height; ++y)
        for (int c = 0; c < 3; ++c)
        {
            const int a = image.rgba[image.at(0, y) + static_cast<std::size_t>(c)];
            const int b = image.rgba[image.at(image.width - 1, y) + static_cast<std::size_t>(c)];
            worst = std::max(worst, std::abs(a - b));
        }
    return worst <= tolerance;
}

void testSurfaces()
{
    using CnaRoom::Assets::SurfaceImages;
    using CnaRoom::Assets::TextureBaker;
    const int size = 128;

    const SurfaceImages plaster = TextureBaker::plaster(size, 1u, 0.93f, 0.90f, 0.85f);
    check(plaster.albedo.width == size && plaster.normal.width == size && plaster.orm.width == size, "plaster images sized");
    check(meanChannel(plaster.albedo, 0) > 0.7f, "plaster is light");
    check(std::fabs(meanChannel(plaster.normal, 2) - 1.0f) < 0.15f, "plaster normal map points mostly up (blue)");
    check(tilesHorizontally(plaster.albedo, 40), "plaster wraps horizontally");

    const SurfaceImages oak = TextureBaker::oakPlanks(size, 2u);
    check(meanChannel(oak.albedo, 0) > meanChannel(oak.albedo, 2), "oak is warmer than it is blue");
    check(oak.tileMetres > 0.5f, "oak tile is metric");

    const SurfaceImages grass = TextureBaker::grass(size, 3u);
    check(meanChannel(grass.albedo, 1) > meanChannel(grass.albedo, 0) && meanChannel(grass.albedo, 1) > meanChannel(grass.albedo, 2),
          "grass is green");

    // Window interiors: the lit room is brighter than the dark one, the drawn
    // curtains warm, the television cool, and every kind keeps the glass's
    // low roughness in its ORM.
    const SurfaceImages litRoom = TextureBaker::windowInterior(size, 9u, 0);
    const SurfaceImages drawn = TextureBaker::windowInterior(size, 9u, 1);
    const SurfaceImages television = TextureBaker::windowInterior(size, 9u, 2);
    const SurfaceImages darkRoom = TextureBaker::windowInterior(size, 9u, 3);
    check(meanChannel(litRoom.albedo, 0) > meanChannel(darkRoom.albedo, 0) * 1.5f, "a lit window is brighter than a dark one");
    check(meanChannel(drawn.albedo, 0) > meanChannel(drawn.albedo, 2) * 1.2f, "drawn curtains read warm");
    // The curtains dominate the mean of every kind, so the television's cool
    // wash shows as a blue-minus-red shift against the dark room with the same
    // curtains (same seed), not as an absolute hue.
    const float televisionShift = meanChannel(television.albedo, 2) - meanChannel(television.albedo, 0);
    const float darkShift = meanChannel(darkRoom.albedo, 2) - meanChannel(darkRoom.albedo, 0);
    check(televisionShift > darkShift + 0.005f, "a television window reads cooler than a dark one, shift " + std::to_string(televisionShift - darkShift));
    check(meanChannel(darkRoom.orm, 1) < 0.2f, "window glass is glossy in the ORM");
    const SurfaceImages litGlow = TextureBaker::windowInterior(size, 9u, 0, true);
    const SurfaceImages coolGlow = TextureBaker::windowInterior(size, 9u, 2, true);
    check(meanChannel(litGlow.albedo, 0) > meanChannel(litRoom.albedo, 0) * 1.4f, "the night bake of a lit room is brighter than its day albedo");
    check(meanChannel(litGlow.albedo, 0) > meanChannel(litGlow.albedo, 2) * 1.2f, "the lamp-lit room glows warm");
    check(meanChannel(coolGlow.albedo, 2) - meanChannel(coolGlow.albedo, 0) > televisionShift, "the television room's night bake is cooler still");

    // Canvases: the three kinds are distinct paintings, matte, with the cloth
    // in their normals.
    const SurfaceImages canvas0 = TextureBaker::canvas(size, 3u, 0), canvas1 = TextureBaker::canvas(size, 3u, 1), canvas2 = TextureBaker::canvas(size, 3u, 2);
    const auto meanDistance = [&](const SurfaceImages& a, const SurfaceImages& b) {
        float d = 0.0f;
        for (int c = 0; c < 3; ++c) d += std::fabs(meanChannel(a.albedo, c) - meanChannel(b.albedo, c));
        return d;
    };
    // Kinds 0 and 2 mirror each other in the mean (warm fields on a cool
    // ground, cool fields on a warm one), so their grounds are compared where
    // no field reaches: the top strip.
    check(meanDistance(canvas0, canvas1) > 0.15f && meanDistance(canvas1, canvas2) > 0.15f, "the sage canvas differs from the other two");
    check(meanChannelRows(canvas0.albedo, 2, 0.0f, 0.04f) > meanChannelRows(canvas0.albedo, 0, 0.0f, 0.04f) + 0.03f, "canvas 0 has a cool ground");
    check(meanChannelRows(canvas2.albedo, 0, 0.0f, 0.02f) > meanChannelRows(canvas2.albedo, 2, 0.0f, 0.02f) + 0.03f, "canvas 2 has a warm ground");
    check(meanChannelRows(canvas1.albedo, 0, 0.0f, 0.05f) > 0.7f, "canvas 1 has a light ground");
    check(meanChannel(canvas1.albedo, 1) > meanChannel(canvas1.albedo, 2), "the sage canvas is greener than it is blue");
    check(meanChannel(canvas0.orm, 1) > 0.6f && meanChannel(canvas0.orm, 1) < 0.8f, "canvas paint is matte");
    check(std::fabs(meanChannel(canvas0.normal, 2) - 1.0f) < 0.15f, "canvas normals point mostly up");

    // The knit: cream wool, rough, with ribs (the albedo's column means swing
    // with the ribs) and a relief (the normals lean away from straight up).
    const SurfaceImages knit = TextureBaker::knit(size, 5u, 0.74f, 0.68f, 0.58f);
    check(meanChannel(knit.albedo, 0) > meanChannel(knit.albedo, 2) + 0.05f, "the knit is warm");
    check(meanChannel(knit.orm, 1) > 0.85f, "the knit is rough");
    {
        float lightest = 0.0f, darkest = 1.0f;
        for (int x = 0; x < size; ++x)
        {
            float column = 0.0f;
            for (int y = 0; y < size; ++y) column += static_cast<float>(knit.albedo.rgba[knit.albedo.at(x, y)]) / 255.0f;
            column /= static_cast<float>(size);
            lightest = std::max(lightest, column);
            darkest = std::min(darkest, column);
        }
        check(lightest - darkest > 0.05f, "the knit's ribs show in its columns, swing " + std::to_string(lightest - darkest));
    }
    check(meanChannel(knit.normal, 2) < 0.97f, "the knit has relief in its normals");

    // The newsprint: a grey-white page, its masthead band dark, its margin
    // clean, and the text columns a mid grey between the two.
    {
        const SurfaceImages page = TextureBaker::newsprint(size, 11u);
        const float masthead = meanChannelRows(page.albedo, 0, 0.04f, 0.10f);
        const float margin = meanChannelRows(page.albedo, 0, 0.975f, 1.0f);
        const float text = meanChannelRows(page.albedo, 0, 0.60f, 0.90f);
        check(masthead < 0.55f, "the masthead band is inked, " + std::to_string(masthead));
        check(margin > 0.65f, "the page's bottom margin is clean, " + std::to_string(margin));
        check(text < margin - 0.05f && text > masthead, "the text columns read grey between the two, " + std::to_string(text));
        check(meanChannel(page.orm, 1) > 0.85f, "newsprint is matte");
    }

    // The raindrop flipbook: a phase moves only the runners (most of the map
    // stays), and a whole loop comes back to the first frame.
    {
        const CnaRoom::Assets::Image f0 = TextureBaker::raindrops(size, 8u, 0.0f), fHalf = TextureBaker::raindrops(size, 8u, 0.5f), fLoop = TextureBaker::raindrops(size, 8u, 1.0f);
        std::size_t movedHalf = 0, movedLoop = 0, total = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
        for (std::size_t i = 0; i < f0.rgba.size(); i += 4)
        {
            if (f0.rgba[i] != fHalf.rgba[i] || f0.rgba[i + 1] != fHalf.rgba[i + 1]) ++movedHalf;
            if (f0.rgba[i] != fLoop.rgba[i] || f0.rgba[i + 1] != fLoop.rgba[i + 1]) ++movedLoop;
        }
        const float halfShare = static_cast<float>(movedHalf) / static_cast<float>(total), loopShare = static_cast<float>(movedLoop) / static_cast<float>(total);
        check(halfShare > 0.01f && halfShare < 0.5f, "half a loop moves the runners and leaves the rest: " + std::to_string(halfShare));
        check(loopShare < 0.001f, "a whole loop returns to the first frame: " + std::to_string(loopShare));
    }

    // The clock dial: round (transparent corners), light inside, dark at the
    // 12 o'clock bar.
    const SurfaceImages dial = TextureBaker::clockDial(size, 5u);
    check(dial.albedo.rgba[3] == 0, "the dial's corner is outside the disc");
    const float dialCoverage = coverage(dial.albedo);
    check(dialCoverage > 0.70f && dialCoverage < 0.85f, "the dial covers pi/4 of its quad, " + std::to_string(dialCoverage));
    check(meanChannelRows(dial.albedo, 0, 0.45f, 0.55f) > 0.6f, "the dial's face is light across its middle");
    check(dial.albedo.rgba[((size / 2) * size + size / 2 - (size * 2) / 5) * 4] < 60, "a bar sits at 9 o'clock");

    const SurfaceImages leaves = TextureBaker::foliage(size, 4u, 0.75f);
    const float cover = coverage(leaves.albedo);
    check(cover > 0.35f && cover < 0.9f, "foliage has gaps, coverage " + std::to_string(cover));
    const SurfaceImages dense = TextureBaker::foliage(size, 4u, 1.0f);
    check(coverage(dense.albedo) >= cover, "coverage parameter fills the mask");
    const SurfaceImages dusted = TextureBaker::foliage(size, 4u, 0.75f, 1.0f);
    check(meanChannel(dusted.albedo, 2) > meanChannel(leaves.albedo, 2) + 0.05f, "snow-dusted leaves are bluer/brighter");
    check(std::fabs(coverage(dusted.albedo) - cover) < 0.02f, "snow keeps the leaf mask");

    const SurfaceImages snow = TextureBaker::snow(size, 5u);
    check(meanChannel(snow.albedo, 0) > 0.8f && meanChannel(snow.albedo, 2) > 0.8f, "snow is white");
    check(meanChannel(snow.orm, 1) > 0.8f, "snow is rough");

    const SurfaceImages metal = TextureBaker::flat(32, 6u, 0.9f, 0.9f, 0.9f, 0.2f, 1.0f, 0.02f);
    check(meanChannel(metal.orm, 2) > 0.95f, "metal flag in the ORM blue channel");
    check(std::fabs(meanChannel(metal.orm, 1) - 0.2f) < 0.05f, "roughness in the ORM green channel");

    const CnaRoom::Assets::Image drops = TextureBaker::raindrops(size, 7u);
    check(drops.width == size, "raindrop normal map sized");
    check(meanChannel(drops, 2) > 0.85f, "raindrops mostly flat between drops");

    const SurfaceImages slabs = TextureBaker::pavingSlabs(size, 8u);
    check(std::fabs(slabs.tileMetres - 1.2f) < 1e-4f, "paving tile is 1.2 m");
    const SurfaceImages roof = TextureBaker::roofTiles(size, 9u, 0.55f, 0.30f, 0.20f);
    check(meanChannel(roof.albedo, 0) > meanChannel(roof.albedo, 2), "terracotta is red");
}

void testMips()
{
    using CnaRoom::Assets::Image;
    // A half-covered checker keeps ~50 % coverage when box-filtered; the
    // upload path rescales alpha per level, exercised here through halved().
    Image checker(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            checker.set(x, y, 0.5f, 0.5f, 0.5f, ((x / 4 + y / 4) % 2) ? 1.0f : 0.0f);
    check(std::fabs(coverage(checker) - 0.5f) < 0.01f, "checker coverage 0.5");
    const Image half = checker.halved(true);
    check(half.width == 32 && half.height == 32, "halved size");
    check(std::fabs(meanChannel(half, 3) - 0.5f) < 0.02f, "box filter keeps the mean alpha");
    // sRGB averaging is done in linear light: two texels 0 and 255 average above 128 in sRGB.
    Image pair(2, 1);
    pair.set(0, 0, 0.0f, 0.0f, 0.0f);
    pair.set(1, 0, 1.0f, 1.0f, 1.0f);
    const Image one = pair.halved(true);
    check(one.rgba[0] > 170, "linear-light average of black and white is a light grey, got " + std::to_string(one.rgba[0]));
    const Image linear = pair.halved(false);
    check(std::abs(static_cast<int>(linear.rgba[0]) - 128) <= 1, "plain average of black and white is mid grey");
}

}  // namespace

int main()
{
    testSurfaces();
    testMips();
    if (failures == 0) std::printf("cna_room_baker_tests: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
