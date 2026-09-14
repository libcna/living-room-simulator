// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class SpriteBatch;
    class Texture2D;
}

namespace CnaRoom {

/**
 * @brief On-screen text from a procedural 5×7 bitmap font.
 *
 * CNA ships no default SpriteFont and this project fetches no font files, so
 * the glyphs are defined here as rows of bits, uploaded once as a small
 * atlas and drawn with SpriteBatch. Upper-case letters, digits and the
 * punctuation a statistics panel needs; lower-case is drawn as upper-case.
 */
class Overlay
{
public:
    explicit Overlay(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    /// Draws the lines top-left with a translucent backing; `scale` is the pixel size of one font pixel.
    void draw(const std::vector<std::string>& lines, int scale = 2);

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> batch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> atlas_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
};

}  // namespace CnaRoom
