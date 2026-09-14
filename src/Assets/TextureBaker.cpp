// SPDX-License-Identifier: MIT
#include "CnaRoom/Assets/TextureBaker.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaRoom::Assets {

namespace {

constexpr float kPi = 3.14159265358979f;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

/// Writes ORM = (occlusion, roughness, metallic).
void packOrm(Image& orm, int x, int y, float occlusion, float roughness, float metallic)
{
    orm.set(x, y, clamp01(occlusion), clamp01(roughness), clamp01(metallic), 1.0f);
}

}  // namespace

SurfaceImages TextureBaker::plaster(int size, std::uint32_t seed, float r, float g, float b)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 2.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float coarse = Noise::fbm(u, v, 3, 3, 0.5f, seed);           // roller marks
            const float fine = Noise::fbm(u, v, 48, 4, 0.55f, seed + 3u);      // sand grain
            const float pores = Noise::cellular(u, v, 90, seed + 5u);
            const float h = coarse * 0.35f + fine * 0.5f + (1.0f - clamp01(pores * 2.2f)) * 0.15f;
            height.ref(x, y) = h;
            const float tint = 0.96f + 0.08f * (coarse - 0.5f) + 0.03f * (fine - 0.5f);
            out.albedo.set(x, y, clamp01(r * tint), clamp01(g * tint), clamp01(b * tint));
            const float rough = 0.78f + 0.14f * fine + 0.05f * (coarse - 0.5f);
            packOrm(out.orm, x, y, 1.0f - 0.08f * (1.0f - h), rough, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 2.2f);
    return out;
}

SurfaceImages TextureBaker::oakPlanks(int size, std::uint32_t seed)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 2.4f;  // tile covers 2.4 m: planks 1.2 m long x 0.15 m wide
    constexpr int kPlanksAcross = 16;   // 2.4 / 16 = 0.15 m wide
    constexpr int kPlanksAlong = 2;     // 1.2 m long
    const float plankWidth = out.tileMetres / kPlanksAcross;
    const float plankLength = out.tileMetres / kPlanksAlong;
    constexpr float kGapMetres = 0.0010f, kBevelMetres = 0.0035f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const int plankRow = static_cast<int>(v * kPlanksAcross);
            // Stagger every other row by half a plank length.
            const float stagger = (plankRow % 2) ? 0.5f / kPlanksAlong : 0.0f;
            const float along = std::fmod(u + stagger + 1.0f, 1.0f);
            const int plankCol = static_cast<int>(along * kPlanksAlong);
            const std::uint32_t plankId = static_cast<std::uint32_t>(plankRow * 31 + plankCol * 7);
            const float id = Noise::hash(plankRow, plankCol, seed);
            const float id2 = Noise::hash(plankRow, plankCol, seed + 11u);

            // Grain: noise stretched along the plank, cathedral figure from a
            // slow sine across it, both perturbed per plank.
            const float withinRow = v * kPlanksAcross - static_cast<float>(plankRow);
            const float withinCol = along * kPlanksAlong - static_cast<float>(plankCol);
            const float gu = withinCol + id * 7.0f;          // along the plank, 0..1 per plank
            const float gv = withinRow + id2 * 3.0f;         // across
            const float wobble = Noise::fbm(gu * 0.5f, gv * 2.0f, 6, 3, 0.5f, seed + plankId) - 0.5f;
            const float rings = 0.5f + 0.5f * std::sin((gv * 2.2f + wobble * 1.4f + gu * 0.35f) * 2.0f * kPi);
            const float ringsSharp = std::pow(rings, 2.5f);
            const float streaks = Noise::fbm(gu * 0.25f, gv * 6.0f, 24, 3, 0.5f, seed + 21u + plankId);
            const float pores = Noise::fbm(gu, gv * 3.0f, 96, 2, 0.5f, seed + 33u);

            // Gaps and bevels between planks, in metres.
            const float dV = std::min(withinRow, 1.0f - withinRow) * plankWidth;
            const float dU = std::min(withinCol, 1.0f - withinCol) * plankLength;
            const float d = std::min(dV, dU);
            const float bevel = clamp01((d - kGapMetres) / kBevelMetres);
            const float bevelSmooth = bevel * bevel * (3.0f - 2.0f * bevel);

            // Colour: oak, per-plank hue/brightness variation, darker late wood.
            const float bright = 0.84f + 0.28f * (id - 0.5f) - 0.16f * ringsSharp - 0.10f * (streaks - 0.5f) - 0.04f * pores;
            const float warm = 0.92f + 0.14f * (id2 - 0.5f);
            float cr = 0.60f * bright * warm, cg = 0.43f * bright, cb = 0.27f * bright / warm;
            const float gapShade = 0.30f + 0.70f * bevelSmooth;
            out.albedo.set(x, y, clamp01(cr * gapShade), clamp01(cg * gapShade), clamp01(cb * gapShade));

            height.ref(x, y) = bevelSmooth * (0.85f + 0.08f * ringsSharp + 0.05f * streaks) + 0.02f * (id - 0.5f);
            // Varnished: fairly smooth, slightly rougher on late wood and in gaps.
            const float rough = 0.28f + 0.10f * ringsSharp + 0.06f * (streaks - 0.5f) + 0.5f * (1.0f - bevelSmooth);
            packOrm(out.orm, x, y, 0.65f + 0.35f * bevelSmooth, rough, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 2.0f);
    return out;
}

SurfaceImages TextureBaker::carpet(int size, std::uint32_t seed, float r, float g, float b)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 0.6f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float tufts = Noise::cellular(u, v, 140, seed);
            const float fibre = Noise::fbm(u, v, 200, 3, 0.6f, seed + 2u);
            const float wear = Noise::fbm(u, v, 2, 3, 0.5f, seed + 9u);
            const float h = (1.0f - clamp01(tufts * 1.6f)) * 0.6f + fibre * 0.4f;
            height.ref(x, y) = h;
            const float shade = 0.80f + 0.30f * h + 0.10f * (wear - 0.5f);
            out.albedo.set(x, y, clamp01(r * shade), clamp01(g * shade), clamp01(b * shade));
            packOrm(out.orm, x, y, 0.75f + 0.25f * h, 0.92f - 0.06f * h, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 1.6f);
    return out;
}

SurfaceImages TextureBaker::signboard(int size, std::uint32_t seed)
{
    // A cream board (wide: the sign is 8:1, the texture stretches) with a
    // word of dark blocks across its middle: letters of random width with
    // gaps, a few with a notch, so it reads as lettering at 20 m.
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 1.0f;
    std::uint32_t state = seed * 2654435761u + 12345u;
    const auto next = [&] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return static_cast<float>(state & 0xFFFFFFu) / 16777216.0f; };
    struct Letter { float u0, u1; int notch; };
    std::vector<Letter> letters;
    float u = 0.08f;
    while (u < 0.90f)
    {
        const float w = 0.035f + 0.045f * next();
        if (u + w > 0.92f) break;
        letters.push_back({u, u + w, static_cast<int>(next() * 3.0f)});
        u += w + 0.014f + 0.02f * (next() < 0.2f ? 1.0f : 0.0f);   // a word gap now and then
    }
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float px = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), py = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            bool ink = false;
            if (py > 0.30f && py < 0.70f)
                for (const Letter& l : letters)
                {
                    if (px < l.u0 || px > l.u1) continue;
                    // A notch cuts a hole or a bar out of the block: an eye, a crossbar.
                    const float lu = (px - l.u0) / (l.u1 - l.u0), lv = (py - 0.30f) / 0.40f;
                    if (l.notch == 1 && lu > 0.3f && lu < 0.7f && lv > 0.35f && lv < 0.65f) continue;
                    if (l.notch == 2 && lv > 0.45f && lv < 0.55f && lu > 0.15f && lu < 0.85f) continue;
                    ink = true;
                }
            const float grain = Noise::fbm(px, py, 12, 2, 0.5f, seed + 3u);
            const float shade = 0.92f + 0.08f * (grain - 0.5f);
            if (ink) out.albedo.set(x, y, 0.10f, 0.10f, 0.12f);
            else out.albedo.set(x, y, clamp01(0.93f * shade), clamp01(0.88f * shade), clamp01(0.74f * shade));
            height.ref(x, y) = ink ? 0.0f : 0.4f;
            packOrm(out.orm, x, y, 1.0f, ink ? 0.45f : 0.6f, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 0.6f);
    return out;
}

SurfaceImages TextureBaker::knit(int size, std::uint32_t seed, float r, float g, float b)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 0.12f;
    constexpr int kRibs = 12, kRows = 14;   // a stitch a centimetre, rows a little shorter
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const int rib = static_cast<int>(u * kRibs);
            const float across = 0.5f + 0.5f * std::sin(u * kRibs * 2.0f * kPi);                       // the rib's round
            const float bead = 0.55f + 0.45f * std::sin(v * kRows * 2.0f * kPi + (rib % 2 ? kPi : 0.0f));  // stitches staggered rib to rib
            const float fuzz = Noise::fbm(u, v, 120, 3, 0.5f, seed);
            const float wear = Noise::fbm(u, v, 3, 3, 0.5f, seed + 4u);
            const float h = across * bead * 0.8f + fuzz * 0.2f;
            height.ref(x, y) = h;
            const float shade = 0.78f + 0.30f * h + 0.08f * (wear - 0.5f);
            out.albedo.set(x, y, clamp01(r * shade), clamp01(g * shade), clamp01(b * shade));
            packOrm(out.orm, x, y, 0.7f + 0.3f * h, 0.92f - 0.05f * h, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 2.2f);
    return out;
}

SurfaceImages TextureBaker::fabricWeave(int size, std::uint32_t seed, float r, float g, float b)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 0.25f;
    constexpr int kThreads = 80;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float warp = 0.5f + 0.5f * std::sin(u * kThreads * 2.0f * kPi);
            const float weft = 0.5f + 0.5f * std::sin(v * kThreads * 2.0f * kPi);
            const int cu = static_cast<int>(u * kThreads), cv = static_cast<int>(v * kThreads);
            const float over = ((cu + cv) % 2) ? warp : weft;
            const float fuzz = Noise::fbm(u, v, 160, 3, 0.5f, seed);
            const float slub = Noise::fbm(u, v, 6, 3, 0.5f, seed + 4u);
            const float h = over * 0.7f + fuzz * 0.3f;
            height.ref(x, y) = h;
            const float shade = 0.85f + 0.2f * h + 0.08f * (slub - 0.5f);
            out.albedo.set(x, y, clamp01(r * shade), clamp01(g * shade), clamp01(b * shade));
            packOrm(out.orm, x, y, 0.8f + 0.2f * h, 0.85f, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 1.2f);
    return out;
}

SurfaceImages TextureBaker::paintedWood(int size, std::uint32_t seed, float r, float g, float b,
                                        float roughness)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 1.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float brush = Noise::fbm(u * 0.2f, v, 40, 3, 0.5f, seed);        // brush strokes along v
            const float grain = Noise::fbm(u * 0.1f, v, 12, 3, 0.5f, seed + 8u);   // wood underneath
            const float h = brush * 0.6f + grain * 0.4f;
            height.ref(x, y) = h;
            const float shade = 0.97f + 0.05f * (h - 0.5f);
            out.albedo.set(x, y, clamp01(r * shade), clamp01(g * shade), clamp01(b * shade));
            packOrm(out.orm, x, y, 1.0f, roughness + 0.08f * (brush - 0.5f), 0.0f);
        }
    out.normal = normalMapFromHeight(height, 0.9f);
    return out;
}

SurfaceImages TextureBaker::woodGrain(int size, std::uint32_t seed, float r, float g, float b,
                                      float roughness)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 1.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float wobble = Noise::fbm(u * 0.5f, v * 3.0f, 6, 3, 0.5f, seed) - 0.5f;
            const float rings = 0.5f + 0.5f * std::sin((v * 9.0f + wobble * 1.8f + u * 0.4f) * 2.0f * kPi);
            const float ringsSharp = std::pow(rings, 2.2f);
            const float streaks = Noise::fbm(u * 0.3f, v * 6.0f, 32, 3, 0.5f, seed + 7u);
            const float pores = Noise::fbm(u, v * 2.0f, 128, 2, 0.5f, seed + 9u);
            const float shade = 1.0f - 0.22f * ringsSharp - 0.12f * (streaks - 0.5f) - 0.05f * pores;
            out.albedo.set(x, y, clamp01(r * shade), clamp01(g * shade), clamp01(b * shade));
            height.ref(x, y) = 0.5f + 0.1f * ringsSharp + 0.06f * streaks + 0.03f * pores;
            packOrm(out.orm, x, y, 1.0f, clamp01(roughness + 0.08f * ringsSharp + 0.04f * (streaks - 0.5f)), 0.0f);
        }
    out.normal = normalMapFromHeight(height, 1.2f);
    return out;
}

SurfaceImages TextureBaker::concrete(int size, std::uint32_t seed)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 3.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float patches = Noise::fbm(u, v, 4, 4, 0.5f, seed);
            const float grain = Noise::fbm(u, v, 64, 3, 0.5f, seed + 1u);
            const float pits = 1.0f - clamp01(Noise::cellular(u, v, 60, seed + 2u) * 2.5f);
            const float h = patches * 0.5f + grain * 0.4f - pits * 0.3f;
            height.ref(x, y) = h;
            const float shade = 0.52f + 0.18f * (patches - 0.5f) + 0.06f * (grain - 0.5f) - 0.1f * pits;
            out.albedo.set(x, y, clamp01(shade), clamp01(shade * 0.99f), clamp01(shade * 0.96f));
            packOrm(out.orm, x, y, 1.0f - 0.3f * pits, 0.85f + 0.1f * grain, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 2.5f);
    return out;
}

SurfaceImages TextureBaker::brick(int size, std::uint32_t seed)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 1.0f;  // ~4 bricks across (0.25 m), 13 courses (0.075 m)
    constexpr int kAcross = 4, kCourses = 13;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const int course = static_cast<int>(v * kCourses);
            const float stagger = (course % 2) ? 0.5f / kAcross : 0.0f;
            const float along = std::fmod(u + stagger + 1.0f, 1.0f);
            const int col = static_cast<int>(along * kAcross);
            const float wu = along * kAcross - static_cast<float>(col);
            const float wv = v * kCourses - static_cast<float>(course);
            const float mortarU = 0.10f, mortarV = 0.14f;
            const bool mortar = wu < mortarU || wv < mortarV;
            const float id = Noise::hash(col, course, seed);
            const float grain = Noise::fbm(u, v, 90, 3, 0.5f, seed + 3u);
            float cr, cg, cb, h, rough;
            if (mortar)
            {
                const float m = 0.62f + 0.1f * (grain - 0.5f);
                cr = m; cg = m * 0.97f; cb = m * 0.92f; h = 0.2f + 0.1f * grain; rough = 0.95f;
            }
            else
            {
                const float t = 0.85f + 0.3f * (id - 0.5f) + 0.1f * (grain - 0.5f);
                cr = 0.58f * t; cg = 0.30f * t; cb = 0.22f * t;
                h = 0.8f + 0.15f * grain; rough = 0.85f + 0.1f * grain;
            }
            height.ref(x, y) = h;
            out.albedo.set(x, y, clamp01(cr), clamp01(cg), clamp01(cb));
            packOrm(out.orm, x, y, mortar ? 0.7f : 1.0f, rough, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 3.0f);
    return out;
}

SurfaceImages TextureBaker::asphalt(int size, std::uint32_t seed)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 3.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float stones = 1.0f - clamp01(Noise::cellular(u, v, 220, seed) * 1.8f);
            const float patches = Noise::fbm(u, v, 3, 3, 0.5f, seed + 1u);
            const float h = stones * 0.7f + patches * 0.3f;
            height.ref(x, y) = h;
            const float shade = 0.16f + 0.08f * stones + 0.05f * (patches - 0.5f);
            out.albedo.set(x, y, shade, shade, shade * 1.02f);
            packOrm(out.orm, x, y, 0.9f, 0.9f - 0.1f * stones, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 2.0f);
    return out;
}

SurfaceImages TextureBaker::grass(int size, std::uint32_t seed)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 2.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float blades = Noise::fbm(u, v, 150, 3, 0.6f, seed);
            const float clumps = Noise::fbm(u, v, 6, 3, 0.5f, seed + 1u);
            const float dry = Noise::fbm(u, v, 2, 2, 0.5f, seed + 2u);
            height.ref(x, y) = blades * 0.7f + clumps * 0.3f;
            const float g = 0.28f + 0.22f * blades + 0.1f * (clumps - 0.5f);
            const float r = g * (0.55f + 0.35f * dry);
            const float b = g * 0.35f;
            out.albedo.set(x, y, clamp01(r), clamp01(g), clamp01(b));
            packOrm(out.orm, x, y, 0.8f + 0.2f * blades, 0.9f, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 1.5f);
    return out;
}

SurfaceImages TextureBaker::foliage(int size, std::uint32_t seed, float coverage, float snow)
{
    // Leaf clusters for a tree canopy or a hedge: alpha carries the gaps
    // between clusters (masked at 0.5), colour varies per cluster and per leaf.
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 1.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float clusters = Noise::fbm(u, v, 5, 4, 0.55f, seed);
            const float leaves = Noise::cellular(u, v, 28, seed + 1u);   // 0 at a leaf centre
            const float leafShape = 1.0f - clamp01(leaves * 1.6f);
            const float mask = clusters * 0.65f + leafShape * 0.35f;
            const float alpha = clamp01((mask - (0.62f - 0.30f * coverage)) * 12.0f);
            const float tone = Noise::fbm(u, v, 9, 3, 0.5f, seed + 2u);
            const float g = 0.22f + 0.30f * tone + 0.12f * leafShape;
            float r = g * (0.45f + 0.35f * tone);
            float b = g * 0.30f;
            float gg = g;
            if (snow > 0.0f)
            {
                // Snow settles on the upper, flatter leaves: the brighter part
                // of each leaf turns white, the shaded rest stays green.
                const float settle = clamp01((leafShape - 0.35f) * 2.0f) * snow;
                r = r * (1.0f - settle) + 0.90f * settle;
                gg = g * (1.0f - settle) + 0.92f * settle;
                b = b * (1.0f - settle) + 0.96f * settle;
            }
            height.ref(x, y) = leafShape * 0.6f + clusters * 0.4f;
            out.albedo.set(x, y, clamp01(r), clamp01(gg), clamp01(b), alpha);
            packOrm(out.orm, x, y, 0.55f + 0.45f * clusters, 0.75f + 0.2f * (1.0f - leafShape), 0.0f);
        }
    out.normal = normalMapFromHeight(height, 2.5f);
    return out;
}

SurfaceImages TextureBaker::bark(int size, std::uint32_t seed)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 1.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            // Vertical ridges: stretched value noise (fine around, coarse along the trunk).
            float ridges = 0.0f, amplitude = 0.55f;
            for (int octave = 0; octave < 4; ++octave)
            {
                const int period = 24 << octave;
                ridges += Noise::value(u * static_cast<float>(period), v * static_cast<float>(period) * 0.18f, period,
                                       seed + static_cast<std::uint32_t>(octave) * 131u) * amplitude;
                amplitude *= 0.5f;
            }
            const float cracks = Noise::cellular(u, v, 10, seed + 5u);
            const float h = clamp01(ridges * 0.8f + clamp01(cracks * 2.0f) * 0.3f);
            const float tone = 0.16f + 0.18f * h;
            out.albedo.set(x, y, clamp01(tone * 1.15f), clamp01(tone * 0.92f), clamp01(tone * 0.70f));
            height.ref(x, y) = h;
            packOrm(out.orm, x, y, 0.6f + 0.4f * h, 0.9f, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 4.0f);
    return out;
}

SurfaceImages TextureBaker::pavingSlabs(int size, std::uint32_t seed)
{
    // 2 x 2 concrete slabs of 0.6 m per 1.2 m tile with 1 cm joints.
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 1.2f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const int col = static_cast<int>(u * 2.0f), row = static_cast<int>(v * 2.0f);
            const float wu = u * 2.0f - static_cast<float>(col), wv = v * 2.0f - static_cast<float>(row);
            const float edge = std::min(std::min(wu, 1.0f - wu), std::min(wv, 1.0f - wv));
            const float joint = 1.0f - clamp01(edge / 0.02f);   // 1 inside the joint
            const float grain = Noise::fbm(u, v, 40, 4, 0.5f, seed);
            const float dirt = Noise::fbm(u, v, 3, 3, 0.5f, seed + 1u);
            const float id = Noise::hash(col, row, seed + 2u);
            const float tone = (0.52f + 0.08f * (id - 0.5f) + 0.10f * (grain - 0.5f) - 0.08f * dirt) * (1.0f - 0.5f * joint);
            out.albedo.set(x, y, clamp01(tone * 1.02f), clamp01(tone), clamp01(tone * 0.95f));
            height.ref(x, y) = (1.0f - joint) * 0.8f + grain * 0.2f;
            packOrm(out.orm, x, y, 1.0f - 0.5f * joint, 0.85f + 0.1f * grain, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 2.5f);
    return out;
}

SurfaceImages TextureBaker::roofTiles(int size, std::uint32_t seed, float r, float g, float b)
{
    // Overlapping courses: 3 courses of 0.33 m per metre, tiles 0.25 m wide,
    // each course half-staggered, with the curved profile in the height.
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 1.0f;
    constexpr int kCourses = 3, kAcross = 4;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const int course = static_cast<int>(v * kCourses);
            const float wv = v * kCourses - static_cast<float>(course);
            const float stagger = (course % 2) ? 0.5f / kAcross : 0.0f;
            const float along = std::fmod(u + stagger + 1.0f, 1.0f);
            const int col = static_cast<int>(along * kAcross);
            const float wu = along * kAcross - static_cast<float>(col);
            const float profile = 0.5f + 0.5f * std::cos((wu - 0.5f) * 6.2831853f);  // barrel curve
            const float overlap = 1.0f - clamp01(wv * 6.0f);                          // shadow under the course above
            const float id = Noise::hash(col, course, seed);
            const float grain = Noise::fbm(u, v, 60, 3, 0.5f, seed + 1u);
            const float moss = clamp01(Noise::fbm(u, v, 4, 3, 0.5f, seed + 2u) - 0.55f) * 2.0f;
            const float t = (0.85f + 0.25f * (id - 0.5f) + 0.1f * (grain - 0.5f)) * (1.0f - 0.35f * overlap);
            const float cr = r * t * (1.0f - 0.5f * moss), cg = g * t * (1.0f - 0.2f * moss), cb = b * t * (1.0f - 0.6f * moss);
            out.albedo.set(x, y, clamp01(cr), clamp01(cg), clamp01(cb));
            height.ref(x, y) = profile * 0.6f + (1.0f - overlap) * 0.4f;
            packOrm(out.orm, x, y, 1.0f - 0.4f * overlap, 0.8f + 0.15f * grain, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 3.0f);
    return out;
}

Image TextureBaker::raindrops(int size, std::uint32_t seed, float phase)
{
    // Droplets on glass as a tangent-space normal map: hemispherical bumps
    // of varied size scattered on a flat field, plus a few running streaks
    // that slide down the tile by the phase (the same drop set at every
    // phase, so the frames loop), each with a thin trail above it.
    Field height(size, size, 0.0f);
    std::uint32_t rng = seed * 2654435761u + 7u;
    const auto next = [&rng]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<float>(rng & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    };
    const int drops = size * size / 260;
    for (int d = 0; d < drops; ++d)
    {
        const float cx = next() * static_cast<float>(size);
        float cy = next() * static_cast<float>(size);
        const float r = (1.5f + next() * next() * 9.0f) * static_cast<float>(size) / 512.0f;
        const bool running = next() < 0.15f;
        const float stretch = running ? 2.5f + next() * 4.0f : 1.0f;   // a running drop
        const float speed = running ? (next() < 0.7f ? 1.0f : 2.0f) : 0.0f;   // whole tiles per loop, so the loop is seamless
        if (running) cy += phase * speed * static_cast<float>(size);      // down the glass: +y in the image
        const int ri = static_cast<int>(r * stretch) + 1;
        for (int y = -ri; y <= ri; ++y)
            for (int x = -static_cast<int>(r) - 1; x <= static_cast<int>(r) + 1; ++x)
            {
                const float dx = static_cast<float>(x) / r, dy = static_cast<float>(y) / (r * stretch);
                const float q = dx * dx + dy * dy;
                if (q >= 1.0f) continue;
                const float h = std::sqrt(1.0f - q) * r / static_cast<float>(size) * 40.0f;
                float& cell = height.ref(((static_cast<int>(cx) + x) % size + size) % size, ((static_cast<int>(cy) + y) % size + size) % size);
                cell = std::max(cell, h);
            }
        if (running)
        {
            // The trail: a thin ridge left behind up the glass, fading with distance.
            const int trail = static_cast<int>(r * stretch * 3.0f);
            for (int y = -ri - trail; y < -ri; ++y)
            {
                const float fade = static_cast<float>(-ri - y) / static_cast<float>(trail);
                const float h = (1.0f - fade) * r / static_cast<float>(size) * 8.0f;
                for (int x = -1; x <= 1; ++x)
                {
                    float& cell = height.ref(((static_cast<int>(cx) + x) % size + size) % size, ((static_cast<int>(cy) + y) % size + size) % size);
                    cell = std::max(cell, h * (x == 0 ? 1.0f : 0.5f));
                }
            }
        }
    }
    return normalMapFromHeight(height, 3.0f);
}

SurfaceImages TextureBaker::snow(int size, std::uint32_t seed)
{
    // Settled snow: near-white with faint blue shadowing in the hollows, a
    // soft lumpy relief and a scatter of sparkling crystals.
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 1.5f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float lumps = Noise::fbm(u, v, 6, 4, 0.55f, seed);
            const float grain = Noise::fbm(u, v, 90, 2, 0.5f, seed + 1u);
            const float crystal = Noise::cellular(u, v, 120, seed + 2u);
            const float sparkle = crystal < 0.06f ? 1.0f : 0.0f;
            const float shade = 0.86f + 0.10f * lumps + 0.03f * (grain - 0.5f) + 0.08f * sparkle;
            out.albedo.set(x, y, clamp01(shade * 0.97f), clamp01(shade * 0.98f), clamp01(shade * 1.0f + 0.02f));
            height.ref(x, y) = lumps * 0.8f + grain * 0.2f;
            packOrm(out.orm, x, y, 0.85f + 0.15f * lumps, sparkle > 0.5f ? 0.35f : 0.9f, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 1.2f);
    return out;
}

SurfaceImages TextureBaker::windowInterior(int size, std::uint32_t seed, int kind, bool glow)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size, 0.5f);
    out.tileMetres = 1.0f;
    const auto pick = [seed](int i) { return Noise::hash(i, 7, seed); };
    // Layout drawn from the seed: curtain widths, the lamp's place, the set's.
    const float curtainL = kind == 1 ? 0.5f : 0.10f + 0.22f * pick(1);
    const float curtainR = kind == 1 ? 0.5f : 0.08f + 0.22f * pick(2);
    const float lampU = 0.55f + 0.3f * pick(3), lampV = 0.30f + 0.25f * pick(4);
    // The set stays clear of the curtains: a screen half hidden by cloth read
    // as a defect rather than a room.
    const float tvU = std::clamp(0.25f + 0.25f * pick(5), curtainL + 0.16f, 1.0f - curtainR - 0.16f), tvV = 0.35f + 0.15f * pick(6);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float grain = Noise::fbm(u, v, 6, 3, 0.5f, seed + 9u);
            // The room: dark, its ceiling a little brighter than its floor. The
            // night bake lifts the whole room, since a lamp lights its walls to a
            // tenth of the shade or so and a lit window reads as a warm rectangle
            // from the street, not as a dot in a dark one.
            float r = 0.028f + 0.03f * (1.0f - v), g = 0.026f + 0.028f * (1.0f - v), b = 0.03f + 0.03f * (1.0f - v);
            if (glow && kind == 0)
            {
                r += 0.10f + 0.16f * (1.0f - v);
                g += 0.08f + 0.13f * (1.0f - v);
                b += 0.05f + 0.08f * (1.0f - v);
            }
            else if (glow && kind == 2)
            {
                r += 0.04f + 0.04f * (1.0f - v);
                g += 0.05f + 0.05f * (1.0f - v);
                b += 0.07f + 0.07f * (1.0f - v);
            }
            if (kind == 0)
            {
                // A warm glow over the room from the lamp, and the shade itself.
                const float d = std::sqrt((u - lampU) * (u - lampU) + (v - lampV) * (v - lampV) * 1.6f);
                const float halo = (glow ? 0.35f : 0.18f) * std::exp(-d * d * 8.0f) + 0.10f * (1.0f - v);
                const float shade = clamp01(1.0f - (d - 0.05f) / 0.06f);   // a soft-edged disc
                r += halo * 1.0f + shade * 0.95f;
                g += halo * 0.80f + shade * 0.78f;
                b += halo * 0.55f + shade * 0.52f;
            }
            else if (kind == 2)
            {
                // A television at the far wall: a cool rectangle and its wash.
                const bool screen = std::fabs(u - tvU) < 0.14f && std::fabs(v - tvV) < 0.09f;
                const float dx = std::max(std::fabs(u - tvU) - 0.14f, 0.0f), dy = std::max(std::fabs(v - tvV) - 0.09f, 0.0f);
                const float wash = (glow ? 0.25f : 0.12f) * std::exp(-(dx * dx + dy * dy) * 30.0f);
                r += (screen ? 0.62f : 0.0f) + wash * 0.7f;
                g += (screen ? 0.72f : 0.0f) + wash * 0.8f;
                b += (screen ? 0.90f : 0.0f) + wash * 1.0f;
            }
            // Curtains: cream cloth in vertical folds, hung at the sides (or
            // drawn across the whole window); lit from behind for kind 1.
            const bool curtain = u < curtainL || u > 1.0f - curtainR;
            if (curtain)
            {
                const float fold = 0.72f + 0.28f * std::sin(u * 52.0f + grain * 3.0f);
                const float lit = kind == 1 ? 0.55f + 0.30f * (1.0f - v) + 0.10f * grain : kind == 3 ? 0.18f : glow ? 0.50f : 0.32f;
                r = lit * fold * 1.00f;
                g = lit * fold * 0.90f;
                b = lit * fold * 0.72f;
            }
            r *= 0.94f + 0.12f * grain;
            g *= 0.94f + 0.12f * grain;
            b *= 0.94f + 0.12f * grain;
            out.albedo.set(x, y, clamp01(r), clamp01(g), clamp01(b));
            height.ref(x, y) = 0.5f;
            packOrm(out.orm, x, y, 1.0f, 0.10f, 0.0f);   // the glass in front is what the light sees
        }
    out.normal = normalMapFromHeight(height, 0.0f);
    return out;
}

SurfaceImages TextureBaker::flame(int size, std::uint32_t seed)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size, 0.5f);
    out.tileMetres = 1.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            // t runs 0 at the tip (top) to 1 at the base; the bulb is widest at two thirds down.
            const float t = clamp01((v - 0.04f) / 0.86f);
            const float width = 0.34f * std::sin(kPi * std::pow(t, 0.75f)) + 0.02f;
            const float wobble = (Noise::fbm(u, v, 5, 3, 0.5f, seed) - 0.5f) * 0.12f * (1.0f - t * 0.5f);
            const float d = std::fabs(u + wobble - 0.5f) / std::max(width, 1e-3f);
            const float tongues = 0.72f + 0.28f * Noise::fbm(u * 0.7f, v * 1.4f, 7, 3, 0.55f, seed + 3u);
            float alpha = clamp01(1.0f - d * d) * tongues;
            if (v < 0.04f || v > 0.93f) alpha *= clamp01(1.0f - std::fabs(v - 0.485f) / 0.45f) * 2.0f;
            alpha = clamp01(alpha);
            // Colour by distance from the axis: pale core, orange body, red rim.
            const float core = clamp01(1.0f - d / 0.5f), rim = clamp01((d - 0.65f) / 0.35f);
            float r = 1.0f, g = 0.95f * core + 0.55f * (1.0f - core), b = 0.62f * core + 0.10f * (1.0f - core);
            r = r * (1.0f - rim) + 0.85f * rim;
            g = g * (1.0f - rim) + 0.18f * rim;
            b = b * (1.0f - rim) + 0.02f * rim;
            // Hotter toward the base.
            g *= 0.85f + 0.15f * t;
            out.albedo.set(x, y, clamp01(r), clamp01(g), clamp01(b), alpha);
            packOrm(out.orm, x, y, 1.0f, 1.0f, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 0.0f);
    return out;
}

SurfaceImages TextureBaker::embers(int size, std::uint32_t seed)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size);
    out.tileMetres = 0.25f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float cell = Noise::cellular(u, v, 14, seed);           // 0 at a lump's heart, ~1 at the cracks
            const float grain = Noise::fbm(u, v, 30, 3, 0.5f, seed + 2u);
            const float vary = Noise::fbm(u, v, 4, 2, 0.5f, seed + 4u);    // which cracks glow
            const float crack = clamp01((cell - 0.56f) / 0.18f);
            const float heat = clamp01((vary - 0.3f) / 0.4f);          // the hot side of the bed
            const float glow = crack * heat;
            const float charcoal = 0.05f + 0.05f * grain;
            // Lumps: charcoal, or a dull red where the bed is hot; cracks: orange-yellow.
            const float lumpR = charcoal + 0.22f * heat * (1.0f - crack), lumpG = charcoal * 0.9f + 0.03f * heat, lumpB = charcoal * 0.85f;
            const float r = lumpR + (1.00f - lumpR) * glow;
            const float g = lumpG + (0.45f - lumpG) * glow;
            const float b = lumpB + (0.06f - lumpB) * glow;
            out.albedo.set(x, y, clamp01(r), clamp01(g), clamp01(b));
            height.ref(x, y) = 1.0f - cell * 0.8f + grain * 0.15f;
            packOrm(out.orm, x, y, 0.8f + 0.2f * (1.0f - cell), 0.9f - 0.3f * glow, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 1.6f);
    return out;
}

SurfaceImages TextureBaker::clockDial(int size, std::uint32_t seed)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size, 0.5f);
    out.tileMetres = 1.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 0.5f;
            const float v = 0.5f - (static_cast<float>(y) + 0.5f) / static_cast<float>(size);   // up
            const float r = std::sqrt(u * u + v * v);
            const float grain = Noise::fbm(u + 0.5f, v + 0.5f, 24, 3, 0.5f, seed);
            float cr = 0.93f, cg = 0.91f, cb = 0.85f;   // aged cream
            const float tone = 0.96f + 0.06f * grain;
            cr *= tone; cg *= tone; cb *= tone;
            // The angle from 12, clockwise; bars every hour, ticks every minute.
            const float angle = std::atan2(u, v);   // 0 at 12, positive toward 3
            const float turn = angle < 0.0f ? angle + 2.0f * kPi : angle;
            const float hourStep = 2.0f * kPi / 12.0f, minuteStep = 2.0f * kPi / 60.0f;
            const float toHour = std::fabs(std::fmod(turn + hourStep * 0.5f, hourStep) - hourStep * 0.5f);
            const float toMinute = std::fabs(std::fmod(turn + minuteStep * 0.5f, minuteStep) - minuteStep * 0.5f);
            const float tangential = r * toHour, tangentialMinute = r * toMinute;
            const bool hourBar = r > 0.36f && r < 0.44f && tangential < 0.011f;
            const bool minuteTick = r > 0.41f && r < 0.44f && tangentialMinute < 0.004f;
            const bool centreDot = r < 0.012f;
            if (hourBar || minuteTick || centreDot) { cr = 0.10f; cg = 0.10f; cb = 0.11f; }
            // The rim: a dark ring with a lighter bevel, then nothing outside.
            float alpha = 1.0f;
            if (r > 0.455f) { cr = 0.09f; cg = 0.09f; cb = 0.09f; }
            if (r > 0.47f && r < 0.485f) { cr = 0.22f; cg = 0.22f; cb = 0.22f; }
            if (r > 0.5f) alpha = 0.0f;
            out.albedo.set(x, y, clamp01(cr), clamp01(cg), clamp01(cb), alpha);
            height.ref(x, y) = r > 0.455f ? 0.5f + 0.2f * (0.5f - r) / 0.045f : 0.5f;
            packOrm(out.orm, x, y, 1.0f, r > 0.455f ? 0.5f : 0.30f, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 1.0f);
    return out;
}

SurfaceImages TextureBaker::canvas(int size, std::uint32_t seed, int kind)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size, 0.5f);
    out.tileMetres = 1.0f;
    struct Band { float v0, v1, r, g, b; };
    struct Painting { float r, g, b; Band bands[3]; int count; };
    static const Painting kPaintings[3] = {
        {0.16f, 0.19f, 0.24f, {{0.08f, 0.42f, 0.72f, 0.52f, 0.24f}, {0.50f, 0.78f, 0.55f, 0.24f, 0.16f}, {0.84f, 0.95f, 0.70f, 0.68f, 0.60f}}, 3},
        {0.86f, 0.82f, 0.72f, {{0.10f, 0.55f, 0.52f, 0.58f, 0.42f}, {0.62f, 0.90f, 0.30f, 0.34f, 0.24f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}}, 2},
        {0.42f, 0.38f, 0.36f, {{0.05f, 0.30f, 0.36f, 0.22f, 0.30f}, {0.36f, 0.70f, 0.14f, 0.14f, 0.15f}, {0.75f, 0.92f, 0.60f, 0.32f, 0.24f}}, 3},
    };
    const Painting& painting = kPaintings[std::clamp(kind, 0, 2)];
    constexpr int kThreads = 120;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            // Horizontal strokes: slow along u, quick along v; the band edges
            // wander with a coarse noise so no edge is a ruled line.
            const float stroke = Noise::value(u * 8.0f, v * 200.0f, 200, seed + 1u);
            const float wander = Noise::fbm(u, v, 3, 3, 0.5f, seed + 2u) - 0.5f;
            const float grain = Noise::fbm(u, v, 24, 3, 0.5f, seed + 3u);
            float r = painting.r, g = painting.g, b = painting.b, paint = 0.0f;
            for (int i = 0; i < painting.count; ++i)
            {
                const Band& band = painting.bands[i];
                const float edge = 0.025f + 0.02f * grain;
                // Each field is a soft-edged rectangle short of the canvas's sides.
                const float in = std::min({(v - band.v0 - wander * 0.06f) / edge, (band.v1 + wander * 0.05f - v) / edge,
                                           (u - 0.07f - wander * 0.05f) / (edge * 1.5f), (0.93f + wander * 0.05f - u) / (edge * 1.5f)});
                const float weight = clamp01(in) * (0.80f + 0.20f * stroke) * (0.92f + 0.08f * grain);
                r += (band.r - r) * weight;
                g += (band.g - g) * weight;
                b += (band.b - b) * weight;
                paint = std::max(paint, clamp01(in));
            }
            const float shade = 0.95f + 0.10f * (stroke - 0.5f) * paint;
            out.albedo.set(x, y, clamp01(r * shade), clamp01(g * shade), clamp01(b * shade));
            const float weave = 0.5f + 0.5f * std::sin(u * kThreads * 2.0f * kPi) * std::sin(v * kThreads * 2.0f * kPi);
            height.ref(x, y) = 0.5f + 0.04f * (weave - 0.5f) * (1.0f - 0.6f * paint) + 0.05f * (stroke - 0.5f) * paint;
            packOrm(out.orm, x, y, 1.0f, 0.78f - 0.14f * paint, 0.0f);
        }
    out.normal = normalMapFromHeight(height, 0.8f);
    return out;
}

SurfaceImages TextureBaker::flat(int size, std::uint32_t seed, float r, float g, float b,
                                 float roughness, float metallic, float variation)
{
    SurfaceImages out;
    out.albedo = Image(size, size);
    out.orm = Image(size, size);
    Field height(size, size, 0.5f);
    out.tileMetres = 1.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float n = Noise::fbm(u, v, 8, 4, 0.5f, seed);
            const float smudge = Noise::fbm(u, v, 2, 3, 0.5f, seed + 5u);
            const float shade = 1.0f + variation * (n - 0.5f);
            out.albedo.set(x, y, clamp01(r * shade), clamp01(g * shade), clamp01(b * shade));
            height.ref(x, y) = 0.5f + variation * 0.2f * (n - 0.5f);
            packOrm(out.orm, x, y, 1.0f, clamp01(roughness + 0.12f * variation * (smudge - 0.5f)),
                    metallic);
        }
    out.normal = normalMapFromHeight(height, 0.6f);
    return out;
}

namespace {

float coverageAbove(const Image& image, float cutoff, float scale)
{
    std::size_t above = 0, total = 0;
    for (std::size_t i = 3; i < image.rgba.size(); i += 4, ++total)
        if (static_cast<float>(image.rgba[i]) / 255.0f * scale > cutoff) ++above;
    return total == 0 ? 0.0f : static_cast<float>(above) / static_cast<float>(total);
}

/// Scales a level's alpha so its coverage at the cutoff matches `target`.
void matchCoverage(Image& level, float cutoff, float target)
{
    float lo = 0.5f, hi = 8.0f;
    for (int i = 0; i < 12; ++i)
    {
        const float mid = (lo + hi) * 0.5f;
        if (coverageAbove(level, cutoff, mid) < target) lo = mid; else hi = mid;
    }
    const float scale = (lo + hi) * 0.5f;
    for (std::size_t i = 3; i < level.rgba.size(); i += 4)
        level.rgba[i] = static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<float>(level.rgba[i]) * scale), 0L, 255L));
}

}  // namespace

std::unique_ptr<Texture2D> TextureBaker::upload(GraphicsDevice& device, const Image& image,
                                                bool srgb, float alphaCutoff)
{
    auto texture = std::make_unique<Texture2D>(device, image.width, image.height, true,
                                               SurfaceFormat::Color);
    Image level = image;
    const float coverage = alphaCutoff >= 0.0f ? coverageAbove(image, alphaCutoff, 1.0f) : 0.0f;
    int index = 0;
    while (true)
    {
        if (alphaCutoff >= 0.0f && index > 0) matchCoverage(level, alphaCutoff, coverage);
        std::vector<Color> pixels(static_cast<std::size_t>(level.width) * static_cast<std::size_t>(level.height));
        for (std::size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = Color(level.rgba[i * 4], level.rgba[i * 4 + 1], level.rgba[i * 4 + 2],
                              level.rgba[i * 4 + 3]);
        texture->SetData(index, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        if (level.width == 1 && level.height == 1) break;
        level = level.halved(srgb);
        ++index;
    }
    return texture;
}

}  // namespace CnaRoom::Assets
