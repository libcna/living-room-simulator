// SPDX-License-Identifier: MIT
#include "CnaRoom/Assets/Image.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace CnaRoom::Assets {

namespace {

// Transfer functions through tables: the mip chains of ~120 textures at
// 1024^2 call these tens of millions of times per load.
struct SrgbTables
{
    float decode[256];
    std::uint8_t encode[4097];   // linear 0..1 in 1/4096 steps
    SrgbTables()
    {
        for (int i = 0; i < 256; ++i)
        {
            const float c = static_cast<float>(i) / 255.0f;
            decode[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        for (int i = 0; i <= 4096; ++i)
        {
            const float lin = static_cast<float>(i) / 4096.0f;
            const float enc = lin <= 0.0031308f ? lin * 12.92f : 1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f;
            encode[i] = static_cast<std::uint8_t>(std::clamp(enc * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
};

const SrgbTables& tables()
{
    static const SrgbTables t;
    return t;
}

std::uint8_t toSrgb(float lin)
{
    const int i = static_cast<int>(std::clamp(lin, 0.0f, 1.0f) * 4096.0f + 0.5f);
    return tables().encode[i];
}

std::uint8_t quantise(float v)
{
    return static_cast<std::uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
}

}  // namespace

void Image::set(int x, int y, float r, float g, float b, float a)
{
    const std::size_t i = at(x, y);
    rgba[i] = quantise(r);
    rgba[i + 1] = quantise(g);
    rgba[i + 2] = quantise(b);
    rgba[i + 3] = quantise(a);
}

Image Image::halved(bool srgb) const
{
    const int w = std::max(1, width / 2), h = std::max(1, height / 2);
    Image out(w, h);
    const SrgbTables& t = tables();
    const auto rows = [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
        {
            const std::uint8_t* r0 = rgba.data() + static_cast<std::size_t>(std::min(y * 2, height - 1)) * static_cast<std::size_t>(width) * 4u;
            const std::uint8_t* r1 = rgba.data() + static_cast<std::size_t>(std::min(y * 2 + 1, height - 1)) * static_cast<std::size_t>(width) * 4u;
            std::uint8_t* o = out.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u;
            for (int x = 0; x < w; ++x, o += 4)
            {
                const std::size_t x0 = static_cast<std::size_t>(std::min(x * 2, width - 1)) * 4u;
                const std::size_t x1 = static_cast<std::size_t>(std::min(x * 2 + 1, width - 1)) * 4u;
                const std::uint8_t* p0 = r0 + x0;
                const std::uint8_t* p1 = r0 + x1;
                const std::uint8_t* p2 = r1 + x0;
                const std::uint8_t* p3 = r1 + x1;
                if (srgb)
                    for (int c = 0; c < 3; ++c)
                        o[c] = toSrgb((t.decode[p0[c]] + t.decode[p1[c]] + t.decode[p2[c]] + t.decode[p3[c]]) * 0.25f);
                else
                    for (int c = 0; c < 3; ++c)
                        o[c] = static_cast<std::uint8_t>((p0[c] + p1[c] + p2[c] + p3[c] + 2) / 4);
                o[3] = static_cast<std::uint8_t>((p0[3] + p1[3] + p2[3] + p3[3] + 2) / 4);
            }
        }
    };
    // The top levels of a mip chain are the bulk of an asset load; they are
    // split by rows over the cores while the GL thread has nothing to do.
    const unsigned cores = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
    const int rowsPerThread = h / static_cast<int>(cores);
    if (cores > 1 && rowsPerThread >= 32 && static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 65536u)
    {
        std::vector<std::thread> workers;
        workers.reserve(cores - 1);
        for (unsigned i = 1; i < cores; ++i)
        {
            const int y0 = static_cast<int>(i) * rowsPerThread;
            const int y1 = i + 1 == cores ? h : y0 + rowsPerThread;
            workers.emplace_back(rows, y0, y1);
        }
        rows(0, rowsPerThread);
        for (std::thread& worker : workers) worker.join();
    }
    else
        rows(0, h);
    return out;
}

float Field::sample(float x, float y) const
{
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float tx = x - fx, ty = y - fy;
    const float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

namespace Noise {

float hash(int x, int y, std::uint32_t seed)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                      + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

float value(float x, float y, int period, std::uint32_t seed)
{
    const float fx = std::floor(x), fy = std::floor(y);
    int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float tx = x - fx, ty = y - fy;
    const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
    const auto wrap = [period](int v) { return period > 0 ? ((v % period) + period) % period : v; };
    const float a = hash(wrap(ix), wrap(iy), seed), b = hash(wrap(ix + 1), wrap(iy), seed);
    const float c = hash(wrap(ix), wrap(iy + 1), seed), d = hash(wrap(ix + 1), wrap(iy + 1), seed);
    return (a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy;
}

float fbm(float u, float v, int baseFrequency, int octaves, float gain, std::uint32_t seed)
{
    float sum = 0.0f, amplitude = 1.0f, total = 0.0f;
    int frequency = std::max(baseFrequency, 1);
    for (int i = 0; i < octaves; ++i)
    {
        sum += value(u * static_cast<float>(frequency), v * static_cast<float>(frequency),
                     frequency, seed + static_cast<std::uint32_t>(i) * 7919u) * amplitude;
        total += amplitude;
        amplitude *= gain;
        frequency *= 2;
    }
    return total > 0.0f ? sum / total : 0.0f;
}

float cellular(float u, float v, int frequency, std::uint32_t seed)
{
    const float x = u * static_cast<float>(frequency), y = v * static_cast<float>(frequency);
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    float best = 8.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int cx = ix + dx, cy = iy + dy;
            const int wx = ((cx % frequency) + frequency) % frequency;
            const int wy = ((cy % frequency) + frequency) % frequency;
            const float px = static_cast<float>(cx) + hash(wx, wy, seed);
            const float py = static_cast<float>(cy) + hash(wx, wy, seed + 17u);
            const float d = (px - x) * (px - x) + (py - y) * (py - y);
            best = std::min(best, d);
        }
    return std::sqrt(best);
}

}  // namespace Noise

Image normalMapFromHeight(const Field& height, float strength)
{
    Image out(height.width, height.height);
    for (int y = 0; y < height.height; ++y)
        for (int x = 0; x < height.width; ++x)
        {
            const float dx = (height.at(x + 1, y) - height.at(x - 1, y)) * strength;
            const float dy = (height.at(x, y + 1) - height.at(x, y - 1)) * strength;
            // Texture v grows downwards on screen; glTF green points "up" the texture.
            float nx = -dx, ny = dy, nz = 1.0f;
            const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= inv; ny *= inv; nz *= inv;
            out.set(x, y, nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, nz * 0.5f + 0.5f, 1.0f);
        }
    return out;
}

}  // namespace CnaRoom::Assets
