// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/Overlay.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cctype>
#include <cstdint>
#include <optional>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaRoom {

namespace {

constexpr int kGlyphWidth = 5, kGlyphHeight = 7;
constexpr int kFirst = 32, kLast = 95;   // space .. underscore
constexpr int kCount = kLast - kFirst + 1;

// Each glyph: seven rows of five bits, most significant bit on the left.
struct Glyph { char c; const char* rows[kGlyphHeight]; };
constexpr Glyph kGlyphs[] = {
    {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {'%', {"##..#", "##.#.", "...#.", "..#..", ".#...", ".#.##", "#..##"}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {',', {".....", ".....", ".....", ".....", "..##.", "..#..", ".#..."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'.', {".....", ".....", ".....", ".....", ".....", "..##.", "..##."}},
    {'/', {"....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {':', {".....", "..##.", "..##.", ".....", "..##.", "..##.", "....."}},
    {'<', {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'>', {".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'[', {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###."}},
    {']', {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###."}},
    {'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
};

}  // namespace

Overlay::Overlay(GraphicsDevice& device) : device_(device)
{
    batch_ = std::make_unique<SpriteBatch>(device_);
    // Atlas: glyphs side by side with a one-pixel gutter, white on transparent.
    const int cell = kGlyphWidth + 1;
    const int width = cell * kCount, height = kGlyphHeight;
    std::vector<Color> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), Color::Transparent);
    for (const Glyph& g : kGlyphs)
    {
        const int index = static_cast<int>(g.c) - kFirst;
        if (index < 0 || index >= kCount) continue;
        for (int y = 0; y < kGlyphHeight; ++y)
            for (int x = 0; x < kGlyphWidth; ++x)
                if (g.rows[y][x] == '#')
                    pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                           + static_cast<std::size_t>(index * cell + x)] = Color::White;
    }
    atlas_ = std::make_unique<Texture2D>(device_, width, height);
    atlas_->SetData(pixels.data(), static_cast<int>(pixels.size()));
    white_ = std::make_unique<Texture2D>(device_, 1, 1);
    const Color pixel = Color::White;
    white_->SetData(&pixel, 1);
}

Overlay::~Overlay() = default;

void Overlay::draw(const std::vector<std::string>& lines, int scale)
{
    if (lines.empty()) return;
    const int cell = kGlyphWidth + 1;
    std::size_t longest = 0;
    for (const std::string& line : lines) longest = std::max(longest, line.size());
    const int margin = 8;
    const int lineHeight = (kGlyphHeight + 2) * scale;
    const int boxWidth = static_cast<int>(longest) * cell * scale + margin * 2;
    const int boxHeight = static_cast<int>(lines.size()) * lineHeight + margin * 2;
    batch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &SamplerState::PointClamp, nullptr, nullptr);
    batch_->Draw(*white_, Rectangle(4, 4, boxWidth, boxHeight), Color(0, 0, 0, 160));
    int y = 4 + margin;
    for (const std::string& line : lines)
    {
        int x = 4 + margin;
        for (char raw : line)
        {
            const int c = std::toupper(static_cast<unsigned char>(raw));
            const int index = c - kFirst;
            if (index >= 0 && index < kCount)
            {
                batch_->Draw(*atlas_, Rectangle(x, y, kGlyphWidth * scale, kGlyphHeight * scale),
                             std::optional<Rectangle>(Rectangle(index * cell, 0, kGlyphWidth, kGlyphHeight)), Color::White);
            }
            x += cell * scale;
        }
        y += lineHeight;
    }
    batch_->End();
}

}  // namespace CnaRoom
