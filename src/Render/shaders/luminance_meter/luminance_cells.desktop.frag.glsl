#version 330 core

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;

// One fragment per cell of a 64 x 36 grid over the frame (the destination is
// 64 x 36 pixels): the mean of an 8 x 8 grid of taps over the cell's share of
// the source, then Rec.709 luminance. The value leaves as log2(luminance)
// clamped to [-24, 24], as a 24-bit fixed-point code across R, G and B of an
// RGBA8 target, so the readback is a plain Color GetData on every renderer.
// Code 0 means "black or not a number"; ExposureStatistics decodes it.
const vec2 kCells = vec2(64.0, 36.0);
const float kLog2Min = -24.0;
const float kLog2Range = 48.0;
const float kCodeMax = 16777215.0;

void main()
{
    vec2 cell = floor(TexCoord * kCells);
    vec3 sum = vec3(0.0);
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i)
        {
            vec2 uv = (cell + (vec2(float(i), float(j)) + 0.5) / 8.0) / kCells;
            sum += texture(texture1, uv).rgb;
        }
    vec3 mean = sum / 64.0;
    float luminance = dot(mean, vec3(0.2126, 0.7152, 0.0722));
    if (isnan(luminance) || isinf(luminance) || !(luminance > 0.0)) luminance = 0.0;
    float code = luminance > 0.0
        ? clamp((log2(luminance) - kLog2Min) / kLog2Range, 0.0, 1.0)
        : 0.0;
    // min(): code 1.0 rounds to 16777216.0 in float, one past the 24-bit split.
    uint q = min(uint(code * kCodeMax + 0.5), 16777215u);
    uvec3 bytes = uvec3(q >> 16u, q >> 8u, q) & uvec3(255u);
    FragColor = vec4(vec3(bytes) / 255.0, 1.0);
}
