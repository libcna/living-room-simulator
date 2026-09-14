// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/SkySystem.hpp"

#include "CnaRoom/Sim/WeatherSystem.hpp"

#include "CnaRoom/Render/FloatCube.hpp"
#include "CnaRoom/Render/Irradiance.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"

#include "CNA/Graphics/AtmosphericSky.hpp"
#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "System/Diagnostics/Stopwatch.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::AtmosphericSky;
using CNA::Graphics::EnvironmentProcessor;
using CNA::Graphics::FullscreenPass;

using System::Diagnostics::Stopwatch;

namespace CnaRoom {

namespace {

constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

// The body appended after CNA's own atmospheric model. Conventions: every
// "direction" uniform is the direction the light *travels* (as the model
// wants); toSun/toMoon are negated inside.
constexpr const char* kFragmentBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform mat4  uInverseViewProjection;
uniform vec3  uSunDirection;
uniform vec3  uMoonDirection;
uniform float uTurbidity;
uniform float uIntensity;
uniform float uModelScale;
uniform float uTime;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uHaze;
uniform float uMoonPhase;
uniform float uStars;
uniform float uLightning;
uniform vec2  uWind;
uniform float uFlipV;
uniform float uEncodeSrgb;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p, int octaves) {
    float sum = 0.0, amplitude = 0.5, total = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (i >= octaves) break;
        sum += valueNoise(p) * amplitude;
        total += amplitude;
        p = p * 2.07 + vec2(19.1, 7.3);
        amplitude *= 0.5;
    }
    return sum / total;
}

// Cloud deck density along a direction, projected onto a plane at `height`.
float deck(vec3 direction, float height, float scale, float coverage, float sharpness, vec2 drift) {
    if (direction.y < 0.01) return 0.0;
    vec2 at = direction.xz * (height / direction.y) * scale + drift;
    float base = fbm(at, 5);
    float detail = fbm(at * 3.1 + vec2(3.7, 1.1), 3);
    float density = base * 0.75 + detail * 0.25;
    density = smoothstep(1.0 - coverage, 1.0 - coverage + sharpness, density);
    return density * smoothstep(0.0, 0.14, direction.y);
}

vec3 stars(vec3 direction) {
    // A lat/long grid of cells; each cell holds at most one star.
    float lat = asin(clamp(direction.y, -1.0, 1.0));
    float lon = atan(direction.z, direction.x);
    vec2 cellSize = vec2(0.0090, 0.0090 / max(cos(lat), 0.15));
    vec2 coords = vec2(lon, lat) / cellSize;
    vec2 cell = floor(coords);
    vec2 within = fract(coords);
    float seed = hash12(cell);
    if (seed > 0.16) return vec3(0.0);
    vec2 centre = vec2(hash12(cell + 7.1), hash12(cell + 3.3));
    vec2 d = (within - centre) * cellSize * vec2(max(cos(lat), 0.15), 1.0);
    float dist = length(d) / 0.0090;
    float magnitude = hash12(cell + 11.7);
    float size = mix(0.045, 0.14, magnitude * magnitude);
    float star = smoothstep(size, 0.0, dist);
    float twinkle = 0.75 + 0.25 * sin(uTime * (2.0 + 4.0 * hash12(cell + 5.5)) + seed * 40.0);
    float temperature = hash12(cell + 2.2);
    vec3 tint = mix(vec3(0.85, 0.90, 1.0), vec3(1.0, 0.92, 0.80), temperature);
    return tint * star * (0.5 + 2.5 * magnitude * magnitude) * twinkle;
}

void main() {
    vec2 screen = vec2(TexCoord.x, mix(TexCoord.y, 1.0 - TexCoord.y, uFlipV));
    vec4 ray = uInverseViewProjection * vec4(screen * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(ray.xyz / ray.w);

    vec3 toSun = -normalize(uSunDirection);
    vec3 toMoon = -normalize(uMoonDirection);
    float daylight = clamp(toSun.y * 6.0 + 0.15, 0.0, 1.0);
    float nightness = 1.0 - daylight;

    // Day: single-scattered Rayleigh/Mie. Night: the same model lit by the moon,
    // scaled to the moon's brightness, plus a starlight/airglow floor.
    // The single-scattering model has no Earth shadow, so it is faded out by
    // hand once the sun is below the horizon (sky illuminance falls ~10x per
    // 3 degrees of depression; the model reaches zero at -7 degrees).
    float sunFade = smoothstep(-0.12, 0.02, toSun.y);
    sunFade *= sunFade;
    vec3 sky = cnaSkyRadiance(direction, uSunDirection, uTurbidity) * uIntensity * uModelScale * sunFade;
    // Low sun: the single-scattering model turns the whole dome orange, while
    // a real evening sky keeps a blue zenith and a blue-grey anti-solar side
    // (the earth's shadow). Blend the chroma towards twilight blue away from
    // the sun, keeping the model's luminance.
    float lowSun = 1.0 - clamp(toSun.y / 0.17, 0.0, 1.0);
    if (lowSun > 0.0) {
        vec2 dFlat = normalize(direction.xz + vec2(1e-5, 0.0));
        vec2 sFlat = normalize(toSun.xz + vec2(1e-5, 0.0));
        float away = 0.5 - 0.5 * dot(dFlat, sFlat);
        float lum = dot(sky, vec3(0.2126, 0.7152, 0.0722));
        vec3 twilight = lum * vec3(0.60, 0.78, 1.0) / 0.758;
        sky = mix(sky, twilight, lowSun * (0.3 + 0.7 * away) * 0.85);
    }
    float moonUp = clamp(toMoon.y * 4.0 + 0.1, 0.0, 1.0);
    // Night radiometry (1 unit = 25 000 lux): the full moon lights the ground
    // with ~0.25 lux and the moonless sky gives ~0.002 lux. Both are kept
    // about 30x brighter than that so a moonlit room still reads at an
    // exposure a lamp-lit room can share; interior lamps still dominate.
    float moonLight = 0.00040 * (0.15 + 0.85 * uMoonPhase) * moonUp;
    vec3 moonSky = cnaSkyRadiance(direction, uMoonDirection, uTurbidity) * uIntensity * uModelScale * moonLight;
    vec3 airglow = vec3(0.00025, 0.00032, 0.00055) * uIntensity * (0.5 + 0.5 * clamp(direction.y, 0.0, 1.0));
    // Urban sky glow: street lighting scattered back from the air, strongest
    // at the horizon. Deliberately ~50x a real city's 0.5 cd/m^2 so the street
    // stays legible next to a lamp-lit room under one global tonemapper.
    airglow += vec3(0.00090, 0.00075, 0.00055) * uIntensity * (1.0 - smoothstep(0.0, 0.45, abs(direction.y)));
    sky += (moonSky + airglow) * nightness;

    // Twilight: the model goes dark quickly once the sun sets; add the glow
    // that scattered light keeps on the sunward horizon.
    // Civil twilight ends about 6 degrees below the horizon (~3 lux); the
    // glow is gone by 8 degrees.
    float duskFade = clamp(1.0 + toSun.y * 7.0, 0.0, 1.0);
    float dusk = clamp(-toSun.y * 8.0, 0.0, 1.0) * duskFade * duskFade;
    if (dusk > 0.0) {
        float towards = clamp(dot(normalize(vec3(direction.x, 0.0, direction.z)),
                                  normalize(vec3(toSun.x, 0.0, toSun.z))), 0.0, 1.0);
        float band = (1.0 - smoothstep(0.0, 0.30, direction.y)) * smoothstep(-0.1, 0.02, direction.y);
        vec3 afterglow = mix(vec3(0.30, 0.10, 0.04), vec3(0.55, 0.30, 0.12), towards) * pow(towards, 2.5) * band;
        vec3 twilightBlue = vec3(0.020, 0.030, 0.070) * (0.4 + 0.6 * clamp(direction.y, 0.0, 1.0));
        sky += (afterglow + twilightBlue) * uIntensity * dusk;
    }

    // Sun disc and aureole.
    float cosSun = dot(direction, toSun);
    float disc = smoothstep(0.99985, 0.99993, cosSun);
    vec3 sunColour = vec3(1.0, 0.95, 0.88);
    float sunAbove = smoothstep(-0.02, 0.02, toSun.y);
    sky += sunColour * disc * 40.0 * uIntensity * sunAbove;
    sky += sunColour * pow(max(cosSun, 0.0), 350.0) * 1.2 * uIntensity * sunAbove;

    // Stars, moon.
    float cloudMask = 1.0;
    float cosMoon = dot(direction, toMoon);
    vec3 starField = stars(direction) * uStars * nightness * 0.0040 * uIntensity;
    sky += starField;
    float moonDisc = smoothstep(0.99990, 0.99996, cosMoon) * moonUp;
    if (moonDisc > 0.0) {
        // Phase: the lit fraction faces the sun. Terminator from the sun direction
        // projected into the moon disc plane.
        vec3 right = normalize(cross(toMoon, vec3(0.0, 1.0, 0.0)));
        vec3 up = cross(right, toMoon);
        vec3 offset = direction - toMoon * cosMoon;
        vec2 uv = vec2(dot(offset, right), dot(offset, up)) / 0.0089;
        vec2 sunOnDisc = normalize(vec2(dot(toSun, right), dot(toSun, up)) + 1e-5);
        float phaseTerm = dot(uv, sunOnDisc);
        float lit = smoothstep(-(1.0 - 2.0 * uMoonPhase) - 0.15, -(1.0 - 2.0 * uMoonPhase) + 0.15, phaseTerm);
        float maria = 0.75 + 0.25 * fbm(uv * 3.0 + 4.0, 3);
        sky += vec3(0.95, 0.96, 1.0) * moonDisc * (0.05 + 0.95 * lit) * maria * 0.40 * uIntensity;
    }
    sky += vec3(0.9, 0.93, 1.0) * pow(max(cosMoon, 0.0), 900.0) * 0.012 * uIntensity * moonUp * (0.2 + 0.8 * uMoonPhase);

    // Clouds: a low cumulus/stratus deck and a high thin cirrus veil.
    vec2 drift = uWind * uTime * 0.0006;
    float lower = deck(direction, 1400.0, 0.00040, uCloudCoverage, 0.28 + 0.2 * (1.0 - uCloudDensity), drift);
    vec3 toward = normalize(direction + toSun * 0.12);
    float towardSun = deck(toward, 1400.0, 0.00040, uCloudCoverage, 0.28, drift);
    float thickness = clamp(towardSun * (0.9 + 0.6 * uCloudDensity), 0.0, 1.0);

    // Cloud colour: lit tops from the sun, shaded bases from the sky, both
    // fading to a moonlit/dark cloud at night.
    vec3 sunTint = mix(vec3(1.0, 0.55, 0.30), vec3(1.05, 1.02, 0.98), clamp(toSun.y * 3.0, 0.0, 1.0));
    vec3 ambientSky = cnaSkyRadiance(vec3(0.0, 1.0, 0.0), uSunDirection, uTurbidity) * uIntensity * uModelScale * sunFade;
    vec3 litColour = sunTint * (0.9 + 0.2 * daylight) * uIntensity * clamp(toSun.y * 3.0 + 0.05, 0.0, 1.0);
    vec3 shadeColour = ambientSky * (0.9 - 0.5 * uCloudDensity) + vec3(0.02) * uIntensity * daylight;
    vec3 cloudColour = mix(litColour, shadeColour, thickness * (0.55 + 0.4 * uCloudDensity));
    float silver = clamp(lower - thickness, 0.0, 1.0) * pow(max(cosSun, 0.0), 8.0);
    cloudColour += sunTint * silver * 0.8 * uIntensity * sunAbove;
    vec3 nightCloud = (moonSky * 1.3 + airglow * 0.5) * (0.5 + 0.5 * uCloudDensity) + vec3(0.9, 0.93, 1.0) * uIntensity * moonLight * 0.6 * pow(max(cosMoon, 0.0), 6.0);
    cloudColour = mix(nightCloud, cloudColour, daylight);
    cloudColour += vec3(1.0, 1.0, 1.1) * uLightning * 2.5 * uIntensity;

    float cover = clamp(lower, 0.0, 1.0);
    cloudMask = 1.0 - cover * 0.97;
    sky = mix(sky, cloudColour, cover * 0.97);

    float high = deck(direction, 6000.0, 0.00018, uCloudCoverage * 0.6 + 0.08, 0.5, drift * 2.2);
    vec3 cirrus = mix(nightCloud * 1.2, vec3(1.0, 0.99, 1.02) * uIntensity * (0.6 + 0.5 * clamp(toSun.y * 2.0, 0.0, 1.0)), daylight);
    sky = mix(sky, cirrus, high * 0.30);

    // Haze: towards the horizon the sky blends into an average of itself.
    vec3 hazeColour = cnaSkyRadiance(vec3(direction.x, 0.04, direction.z), uSunDirection, uTurbidity) * uIntensity * uModelScale * sunFade;
    hazeColour = mix(nightCloud * 1.5, hazeColour, daylight);
    float hazeAmount = uHaze * (1.0 - smoothstep(0.0, 0.35, direction.y));
    sky = mix(sky, hazeColour, hazeAmount);

    // Below the horizon: a ground plane the exterior geometry normally covers.
    float below = smoothstep(0.01, -0.05, direction.y);
    vec3 ground = mix(hazeColour * 0.6, vec3(0.08, 0.075, 0.07) * uIntensity * (0.003 + 0.997 * daylight), 0.5);
    sky = mix(sky, ground, below);

    sky += vec3(uLightning * 0.35) * uIntensity;

    if (uEncodeSrgb > 0.5) {
        vec3 c = clamp(sky, 0.0, 1.0);
        sky = mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
    }
    FragColor = vec4(sky, 1.0);
}
)";

Vector3 normalised(const Vector3& v, const Vector3& fallback)
{
    const float l2 = v.X * v.X + v.Y * v.Y + v.Z * v.Z;
    if (l2 <= 1e-12f) return fallback;
    const float inv = 1.0f / std::sqrt(l2);
    return Vector3(v.X * inv, v.Y * inv, v.Z * inv);
}

Matrix rotationOnly(const Matrix& view)
{
    Matrix r = view;
    r.M41 = 0.0f;
    r.M42 = 0.0f;
    r.M43 = 0.0f;
    return r;
}

std::uint8_t encode(float linear, float scale)
{
    const float mapped = linear / std::max(scale, 1e-6f);
    return static_cast<std::uint8_t>(std::lround(std::clamp(mapped, 0.0f, 1.0f) * 255.0f));
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

/// Direct sun irradiance on a surface facing it, at the top of the atmosphere, in scene units.
constexpr float kSunPower = 3.6f;

/// Transmittance of sunlight through the atmosphere for a given elevation (sine).
Vector3 transmittance(float sinElevation, float turbidity)
{
    const float up = std::max(sinElevation, 0.0f);
    const float zenithDeg = MathHelper::ToDegrees(std::acos(std::clamp(up, 0.0f, 1.0f)));
    const float airMass = 1.0f / std::max(up + 0.50572f * std::pow(std::max(96.07995f - zenithDeg, 1e-3f), -1.6364f), 1e-4f);
    const Vector3 rayleigh(0.0465f, 0.1085f, 0.2646f);
    const float mie = 0.0252f * std::max(turbidity - 1.0f, 0.2f);
    return Vector3(std::exp(-(rayleigh.X + mie) * airMass), std::exp(-(rayleigh.Y + mie) * airMass),
                   std::exp(-(rayleigh.Z + mie) * airMass));
}

}  // namespace

SkySystem::SkySystem(GraphicsDevice& device) : device_(device) {}

SkySystem::~SkySystem() = default;

bool SkySystem::hasImageBasedLighting() const
{
    return irradiance_ != nullptr && prefiltered_ != nullptr && brdfLut_ != nullptr;
}

Vector3 SkySystem::cubeDirection(CubeMapFace face, float u, float v)
{
    const float a = u * 2.0f - 1.0f;
    const float b = v * 2.0f - 1.0f;
    switch (face)
    {
        case CubeMapFace::PositiveX: return normalised(Vector3(1.0f, -b, -a), Vector3::Right);
        case CubeMapFace::NegativeX: return normalised(Vector3(-1.0f, -b, a), Vector3::Left);
        case CubeMapFace::PositiveY: return normalised(Vector3(a, 1.0f, b), Vector3::Up);
        case CubeMapFace::NegativeY: return normalised(Vector3(a, -1.0f, -b), Vector3::Down);
        case CubeMapFace::PositiveZ: return normalised(Vector3(a, -b, 1.0f), Vector3::Backward);
        case CubeMapFace::NegativeZ: return normalised(Vector3(-a, -b, -1.0f), Vector3::Forward);
    }
    return Vector3::Up;
}

void SkySystem::build()
{
    // Calibrate the model's arbitrary scale: with the sun 60 degrees up, the
    // zenith of a clear sky should read about 0.30 of a sunlit white surface
    // (radiance sunPower * 1 / pi at that elevation) -- roughly the 1:3 ratio
    // of real sky luminance to sunlit ground.
    {
        const Vector3 sun(0.0f, std::sin(MathHelper::ToRadians(60.0f)), -std::cos(MathHelper::ToRadians(60.0f)));
        const Vector3 zenith = AtmosphericSky::radiance(Vector3::Up, Vector3(-sun.X, -sun.Y, -sun.Z), 2.5f);
        const float t = transmittance(sun.Y, 2.5f).Y;
        const float sunlitWhite = kSunPower * t / MathHelper::Pi;
        modelScale_ = zenith.Y > 1e-6f ? 0.30f * sunlitWhite / zenith.Y : 1.0f;
        CNA::Logger::Info("living-room-simulator: sky model zenith " + std::to_string(zenith.Y) + " at a 60 degree sun; scale "
                          + std::to_string(modelScale_));
    }
    if (!device_.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
        || !device_.ExecutesShaderEffectSourceEXT())
    {
        supported_ = false;
        reason_ = "the renderer does not execute shader-effect source, so the sky cannot be drawn";
        CNA::Logger::Warn("living-room-simulator: " + reason_);
        return;
    }
    const std::string source = std::string("#version 300 es\nprecision highp float;\n")
                               + AtmosphericSky::getModelGlsl() + kFragmentBody;
    effect_ = std::make_unique<ShaderEffect>(device_, kVertexSource, source);
    supported_ = effect_->IsEffectValid();
    if (!supported_)
    {
        reason_ = "the sky shader did not compile: " + effect_->GetCompileErrorEXT();
        CNA::Logger::Error("living-room-simulator: " + reason_);
        effect_.reset();
        return;
    }
    fullscreen_ = std::make_unique<FullscreenPass>(device_);
    white_ = std::make_unique<Texture2D>(device_, 1, 1);
    const Color pixel = Color::White;
    white_->SetData(&pixel, 1);
}

namespace {
/// The shader's low-sun chroma blend, for the CPU sky (see the fragment source).
Vector3 twilightChroma(const Vector3& sky, const Vector3& direction, const Vector3& toSun)
{
    const float lowSun = 1.0f - std::clamp(toSun.Y / 0.17f, 0.0f, 1.0f);
    if (lowSun <= 0.0f) return sky;
    const float dl = std::sqrt(direction.X * direction.X + direction.Z * direction.Z) + 1e-5f;
    const float sl = std::sqrt(toSun.X * toSun.X + toSun.Z * toSun.Z) + 1e-5f;
    const float away = 0.5f - 0.5f * (direction.X * toSun.X + direction.Z * toSun.Z) / (dl * sl);
    const float lum = 0.2126f * sky.X + 0.7152f * sky.Y + 0.0722f * sky.Z;
    // A slightly greener blue than before: mixed with the lamps' 2700 K the
    // old chroma went magenta on the interior walls at dusk.
    const Vector3 twilight = Vector3(0.60f, 0.78f, 1.0f) * (lum / 0.758f);
    const float t = lowSun * (0.3f + 0.7f * away) * 0.85f;
    return sky * (1.0f - t) + twilight * t;
}
}  // namespace

Vector3 SkySystem::radiance(const Vector3& direction) const
{
    const Vector3 sunTravel = lighting_.lightDirection;
    const Vector3 moonTravel = lighting_.moonLightDirection;
    const float daylight = clamp01(state_.sunDirection.Y * 6.0f + 0.15f);
    const float fadeT = clamp01((state_.sunDirection.Y + 0.12f) / 0.14f);
    const float sunFadeRoot = fadeT * fadeT * (3.0f - 2.0f * fadeT);
    const float sunFade = sunFadeRoot * sunFadeRoot;
    Vector3 sky;
    const float y = direction.Y;
    if (y >= 0.0f)
    {
        sky = twilightChroma(AtmosphericSky::radiance(direction, sunTravel, state_.turbidity) * (state_.intensity * modelScale_ * sunFade),
                             direction, state_.sunDirection);
        const float moonUp = clamp01(state_.moonDirection.Y * 4.0f + 0.1f);
        const float moonLight = 0.00040f * (0.15f + 0.85f * state_.moonPhase) * moonUp;
        const Vector3 moonSky = AtmosphericSky::radiance(direction, moonTravel, state_.turbidity)
                                * (state_.intensity * modelScale_ * moonLight);
        const float horizonT = clamp01(std::abs(y) / 0.45f);
        const Vector3 airglow = Vector3(0.00025f, 0.00032f, 0.00055f) * (state_.intensity * (0.5f + 0.5f * y))
                                + Vector3(0.00090f, 0.00075f, 0.00055f)
                                      * (state_.intensity * (1.0f - horizonT * horizonT * (3.0f - 2.0f * horizonT)));
        sky = sky + (moonSky + airglow) * (1.0f - daylight);
        // Twilight glow (a directional average of the shader's term).
        const float duskFade = clamp01(1.0f + state_.sunDirection.Y * 7.0f);
        const float dusk = clamp01(-state_.sunDirection.Y * 8.0f) * duskFade * duskFade;
        if (dusk > 0.0f)
            sky = sky + Vector3(0.035f, 0.030f, 0.055f) * (state_.intensity * dusk * (0.4f + 0.6f * y));
        // Clouds: the average of lit and shaded cloud colour where they cover.
        const float cover = state_.cloudCoverage * clamp01(y * 3.0f);
        if (cover > 0.0f)
        {
            const Vector3 zenith = AtmosphericSky::radiance(Vector3::Up, sunTravel, state_.turbidity) * (state_.intensity * modelScale_ * sunFade);
            const float sunLit = clamp01(state_.sunDirection.Y * 3.0f + 0.05f);
            const Vector3 lit = Vector3(1.0f, 0.97f, 0.95f) * (state_.intensity * sunLit);
            const Vector3 shade = zenith * (0.9f - 0.5f * state_.cloudDensity);
            const Vector3 cloud = (lit * 0.45f + shade * 0.55f) * daylight
                                  + (moonSky * 1.3f + airglow * 0.5f) * (1.0f - daylight);
            sky = sky * (1.0f - cover * 0.9f) + cloud * (cover * 0.9f);
        }
    }
    else
    {
        // Ground: sunlit exterior surfaces bounce a fraction of the sky and sun.
        const Vector3 horizon = twilightChroma(AtmosphericSky::radiance(Vector3(direction.X, 0.08f, direction.Z), sunTravel,
                                                                        state_.turbidity) * (state_.intensity * modelScale_ * sunFade),
                                               direction, state_.sunDirection);
        const float fade = clamp01(-y * 2.0f);
        sky = horizon * (0.18f + 0.06f * (1.0f - fade)) * (0.3f + 0.7f * daylight)
              + lighting_.sunColour * (0.10f / MathHelper::Pi) * clamp01(state_.sunDirection.Y);
        sky = sky + Vector3(0.00016f, 0.00018f, 0.00024f) * (state_.intensity * (1.0f - daylight));
    }
    return sky;
}

void SkySystem::computeLighting()
{
    const Vector3 sun = normalised(state_.sunDirection, Vector3::Up);
    const Vector3 moon = normalised(state_.moonDirection, Vector3::Down);
    lighting_.lightDirection = Vector3(-sun.X, -sun.Y, -sun.Z);
    lighting_.moonLightDirection = Vector3(-moon.X, -moon.Y, -moon.Z);
    lighting_.sunElevation = std::asin(std::clamp(sun.Y, -1.0f, 1.0f));
    lighting_.moonElevation = std::asin(std::clamp(moon.Y, -1.0f, 1.0f));
    lighting_.sunUp = sun.Y > -0.02f;
    lighting_.moonUp = moon.Y > 0.0f;
    lighting_.daylight = clamp01(sun.Y * 6.0f + 0.15f);

    // Sun: solar radiance scaled so an overhead sun on a white surface reads
    // about 1.0 × intensity × pi / pi in the tonemapper's terms; the sky model's
    // own scale makes a clear sky ~0.1-0.3, so direct sun sits around 3-4.
    const Vector3 t = transmittance(sun.Y, state_.turbidity);
    const float sunPower = kSunPower * clamp01(sun.Y * 3.0f + 0.05f);
    // Direct sun through the cloud deck: the chance the sun sits in a gap
    // (steeper than the open share, since a deck's gaps are its thin edges),
    // plus what a thin cloud passes forward as a smeared disc. Clear keeps
    // ~90 %, cloudy ~50 %, overcast ~5 %, snow ~3 %, rain ~1 %, a storm
    // none; no floor (an overcast day is diffuse light; the old 8 % floor
    // and 24 % under overcast kept crisp sun shadows on the street under a
    // grey sky). What passes through cloud casts soft shadows
    // (`shadowSoftness` widens the receivers' filter).
    const float cover = state_.cloudCoverage, dense = state_.cloudDensity;
    const float gap = std::pow(1.0f - cover, 1.25f);
    const float thin = (1.0f - dense) * (1.0f - dense) * (1.0f - dense) * 0.2f;
    const float cloudDim = gap + cover * thin;
    // Passing clouds: at any moment the sun is either in a gap (the gap's
    // share of the average, capped at the open sun) or behind a cloud (what
    // the thin cloud passes), swapping over a minute under a broken sky so
    // the daylight breathes; the average over time stays cloudDim.
    const float behind = cloudBehindSun(cover, state_.timeSeconds);
    const float gapBright = std::min(1.0f, gap / std::max(1.0f - cover, 0.05f));
    const float sunNow = (1.0f - behind) * gapBright + behind * thin;
    lighting_.shadowSoftness = clamp01((1.0f - behind) * cover * 0.35f * (1.0f + dense) + behind * (0.55f + 0.45f * dense));
    lighting_.sunColour = Vector3(t.X, t.Y, t.Z) * (sunPower * state_.intensity * sunNow);
    if (!lighting_.sunUp) lighting_.sunColour = Vector3::Zero;

    // Moon: a cool dim key light at night.
    const float moonPower = 0.00060f * (0.15f + 0.85f * state_.moonPhase) * clamp01(moon.Y * 3.0f);
    lighting_.moonColour = Vector3(0.80f, 0.86f, 1.0f) * (moonPower * state_.intensity * std::max(cloudDim, 0.05f))
                           * (1.0f - lighting_.daylight);

    // Ambient: cosine-weighted average of the upper hemisphere.
    Vector3 sum = Vector3::Zero;
    float weight = 0.0f;
    constexpr int kRings = 6, kSectors = 12;
    for (int ring = 0; ring < kRings; ++ring)
    {
        const float theta = (static_cast<float>(ring) + 0.5f) / kRings * MathHelper::PiOver2;
        const float cosT = std::cos(theta), sinT = std::sin(theta);
        for (int sector = 0; sector < kSectors; ++sector)
        {
            const float phi = (static_cast<float>(sector) + 0.5f) / kSectors * MathHelper::TwoPi;
            const Vector3 d(sinT * std::cos(phi), cosT, sinT * std::sin(phi));
            sum = sum + radiance(d) * (cosT * sinT);
            weight += cosT * sinT;
        }
    }
    lighting_.ambientColour = weight > 0.0f ? sum * (1.0f / weight) : Vector3::Zero;
}

void SkySystem::update(const SkyState& state, bool forceRebake)
{
    state_ = state;
    computeLighting();
    const Vector3 sun = normalised(state_.sunDirection, Vector3::Up);
    const float moved = Vector3::Distance(sun, bakedSun_);
    const bool coverageChanged = std::fabs(state_.cloudCoverage - bakedCoverage_) > 0.12f;
    if (forceRebake || environment_ == nullptr || moved > 0.035f || coverageChanged)
    {
        bakeEnvironment();
        bakedSun_ = sun;
        bakedCoverage_ = state_.cloudCoverage;
    }
}

void SkySystem::bakeEnvironment()
{
    const Stopwatch bakeWatch = Stopwatch::StartNew();
    constexpr int kFaceSize = 48;
    // A fresh cube per bake: EasyGL reads a cube back through a framebuffer
    // attachment, which comes back incomplete for a cube that has already
    // been bound for sampling (the previous one is still bound while this
    // runs); a new object reads back fine. The old cube stays valid until
    // the next draw binds the new products.
    environment_ = std::make_unique<TextureCube>(device_, kFaceSize, false, SurfaceFormat::Color);

    std::vector<Vector3> faces(6u * kFaceSize * kFaceSize);
    float peak = 1e-4f;
    for (int index = 0; index < 6; ++index)
        for (int y = 0; y < kFaceSize; ++y)
            for (int x = 0; x < kFaceSize; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / kFaceSize;
                const float v = (static_cast<float>(y) + 0.5f) / kFaceSize;
                const Vector3 d = cubeDirection(static_cast<CubeMapFace>(index), u, v);
                Vector3 r = radiance(d);
                const std::size_t at = (static_cast<std::size_t>(index) * kFaceSize + static_cast<std::size_t>(y)) * kFaceSize
                                       + static_cast<std::size_t>(x);
                faces[at] = r;
                peak = std::max(peak, std::max(r.X, std::max(r.Y, r.Z)));
            }
    environmentScale_ = peak * 1.02f;

    std::vector<Color> face(static_cast<std::size_t>(kFaceSize) * kFaceSize);
    for (int index = 0; index < 6; ++index)
    {
        for (int y = 0; y < kFaceSize; ++y)
            for (int x = 0; x < kFaceSize; ++x)
            {
                const std::size_t at = (static_cast<std::size_t>(index) * kFaceSize + static_cast<std::size_t>(y)) * kFaceSize
                                       + static_cast<std::size_t>(x);
                const Vector3& r = faces[at];
                face[static_cast<std::size_t>(y) * kFaceSize + static_cast<std::size_t>(x)] =
                    Color(static_cast<int>(encode(r.X, environmentScale_)),
                          static_cast<int>(encode(r.Y, environmentScale_)),
                          static_cast<int>(encode(r.Z, environmentScale_)), 255);
            }
        environment_->SetData(static_cast<CubeMapFace>(index), face.data(), static_cast<int>(face.size()));
    }

    if (!device_.SupportsImageBasedLightingEXT())
    {
        irradiance_.reset();
        prefiltered_.reset();
        brdfLut_.reset();
        if (bakeCount_ == 0)
            CNA::Logger::Info("living-room-simulator: renderer has no image based lighting; ambient is a flat term");
        ++bakeCount_;
        return;
    }

    EnvironmentProcessor processor(device_);
    // Sizes and sample counts chosen for a rebake every few seconds of game
    // time: the sky's reflection only reaches exterior surfaces and glass.
    // Diffuse: an exact convolution of the float faces into a half-float cube
    // when the renderer can fill one; the 8-bit product at the moon's peak
    // quantises a night sky's irradiance to a code or two (CNA_FINDINGS R-21).
    irradiance_.reset();
    if (floatCubes_ != nullptr)
    {
        std::vector<Vector3> irradiance = convolveIrradiance(faces, kFaceSize, 12, 16);
        const float invScale = 1.0f / environmentScale_;
        for (Vector3& e : irradiance) e = e * invScale;
        irradiance_ = floatCubes_->upload(irradiance, 16);
    }
    if (irradiance_ == nullptr) irradiance_ = processor.generateIrradiance(environment_.get(), 16, 24);
    prefiltered_ = processor.generatePrefilteredSpecular(environment_.get(), 32, prefilteredMips_, 12);
    prefilteredClasses_.clear();
    if (floatCubes_ != nullptr)
    {
        const float invScale = 1.0f / environmentScale_;
        bool complete = true;
        for (const float roughness : {0.06f, 0.28f, 0.50f, 0.72f, 0.94f})
        {
            std::vector<Vector3> filtered = prefilterSpecular(faces, kFaceSize, 32, roughness, 24);
            for (Vector3& v : filtered) v = v * invScale;
            std::unique_ptr<RenderTargetCube> cube = floatCubes_->upload(filtered, 32);
            if (cube == nullptr) complete = false;
            prefilteredClasses_.push_back(std::move(cube));
        }
        if (!complete) prefilteredClasses_.clear();
    }
    if (brdfLut_ == nullptr)
    {
        brdfLut_ = processor.generateBrdfLut(64, 64);
        // Sanity log: (N.V = 1, roughness ~0) must read ~(1, 0); a table that
        // reads its bias near 1 turns every dielectric into a mirror.
        std::vector<Color> lut(64u * 64u);
        brdfLut_->GetData(lut.data(), static_cast<int>(lut.size()));
        const auto at = [&](int nv, int r) {
            const Color& c = lut[static_cast<std::size_t>(r) * 64u + static_cast<std::size_t>(nv)];
            return std::to_string(c.getRProperty() / 255.0f) + "/" + std::to_string(c.getGProperty() / 255.0f);
        };
        CNA::Logger::Info("living-room-simulator: brdf lut (scale/bias) at N.V 1 r 0: " + at(63, 0) + "; N.V 1 r 0.1: " + at(63, 6)
                          + "; N.V 0.5 r 0.5: " + at(32, 32) + "; N.V 0.1 r 0.9: " + at(6, 57) + "; N.V 0.5 r 0: " + at(32, 0));
    }
    ++bakeCount_;
    lastBakeMs_ = static_cast<float>(static_cast<double>(bakeWatch.getElapsedTicksProperty()) / 10000.0);
    if (bakeCount_ <= 2)
        CNA::Logger::Info("living-room-simulator: sky bake " + std::to_string(bakeCount_) + " (" + std::to_string(lastBakeMs_) + " ms) -- sun ("
                          + std::to_string(lighting_.sunColour.X) + ", " + std::to_string(lighting_.sunColour.Y) + ", "
                          + std::to_string(lighting_.sunColour.Z) + ") ambient (" + std::to_string(lighting_.ambientColour.X)
                          + ", " + std::to_string(lighting_.ambientColour.Y) + ", " + std::to_string(lighting_.ambientColour.Z)
                          + ") environment peak " + std::to_string(environmentScale_));
    if (bakeCount_ <= 2)
    {
        const auto show = [](const Vector3& v) {
            return "(" + std::to_string(v.X) + ", " + std::to_string(v.Y) + ", " + std::to_string(v.Z) + ")";
        };
        Vector3 toSun = normalised(state_.sunDirection, Vector3::Up);
        Vector3 flat(toSun.X, 0.0f, toSun.Z);
        flat = normalised(flat, Vector3::Backward);
        const Vector3 horizonSun(flat.X * 0.995f, 0.1f, flat.Z * 0.995f);
        const Vector3 horizonAway(-flat.X * 0.995f, 0.1f, -flat.Z * 0.995f);
        CNA::Logger::Info("living-room-simulator: sky samples -- zenith " + show(radiance(Vector3::Up)) + " sunward horizon "
                          + show(radiance(horizonSun)) + " opposite horizon " + show(radiance(horizonAway)));
    }
}

void SkySystem::draw(const Matrix& view, const Matrix& projection, int width, int height,
                     bool encodeSrgb, float intensityScale)
{
    if (!supported_ || effect_ == nullptr || fullscreen_ == nullptr) return;
    if (width <= 0 || height <= 0) return;

    const Matrix inverse = Matrix::Invert(rotationOnly(view) * projection);
    effect_->Apply();
    effect_->SetUniformMat4("uInverseViewProjection", &inverse.M11);
    const Vector3& sun = lighting_.lightDirection;
    const Vector3& moon = lighting_.moonLightDirection;
    effect_->SetUniformVec3("uSunDirection", sun.X, sun.Y, sun.Z);
    effect_->SetUniformVec3("uMoonDirection", moon.X, moon.Y, moon.Z);
    effect_->SetUniformFloat("uTurbidity", state_.turbidity);
    effect_->SetUniformFloat("uIntensity", state_.intensity * intensityScale);
    effect_->SetUniformFloat("uModelScale", modelScale_);
    effect_->SetUniformFloat("uTime", state_.timeSeconds);
    effect_->SetUniformFloat("uCloudCoverage", state_.cloudCoverage);
    effect_->SetUniformFloat("uCloudDensity", state_.cloudDensity);
    effect_->SetUniformFloat("uHaze", state_.haze);
    effect_->SetUniformFloat("uMoonPhase", state_.moonPhase);
    effect_->SetUniformFloat("uStars", state_.starVisibility);
    effect_->SetUniformFloat("uLightning", state_.lightning);
    effect_->SetUniformVec2("uWind", state_.windX, state_.windZ);
    // FullscreenPass draws through SpriteBatch whose texture origin is the top
    // left, while clip space has +1 at the top: flip V (cna-street CNA-F7).
    effect_->SetUniformFloat("uFlipV", 1.0f);
    effect_->SetUniformFloat("uEncodeSrgb", encodeSrgb ? 1.0f : 0.0f);

    fullscreen_->drawOverCurrentTarget(white_.get(), effect_.get(), width, height);
}

}  // namespace CnaRoom
