// SPDX-License-Identifier: MIT
#pragma once

namespace CnaRoom {

/// Everything the renderer can be told from outside (command line, hotkeys, settings file).
struct RenderSettings
{
    int windowWidth = 1280;
    int windowHeight = 720;
    bool vsync = true;

    float nearPlane = 0.05f;
    float farPlane = 400.0f;
    float verticalFovDegrees = 60.0f;

    bool hdr = true;
    float exposure = 1.0f;
    int tonemap = 2;            ///< 0 none, 1 Reinhard, 2 ACES, 3 Filmic, 4 Uncharted2
    bool bloom = true;
    float bloomIntensity = 0.08f;   ///< the shades' halos at night exposure were swallowing the lamps
    float bloomThreshold = 3.0f;   ///< display-referred: divided by the exposure before the pass
    /// Lens: a photograph's grain, corner fall-off and a touch of colour
    /// fringing (all 0 for a clean render).
    float filmGrain = 0.015f;
    float chromaticAberration = 0.004f;
    float vignette = 0.12f;
    /// Lens flare ghosts from the frame's bright spots (0 off): strength, the
    /// display-referred brightness that flares (divided by the exposure like
    /// the bloom threshold), and how far apart the ghosts sit.
    float lensFlare = 0.0f;
    float lensFlareThreshold = 6.0f;
    float lensFlareDispersal = 0.3f;
    /// Auto white balance: how much of the illuminant's cast (the overcast
    /// sky's blue-grey by day, the lamps' 2700 K at night) the camera takes
    /// out, 0 none .. 1 all; a camera's "auto" leaves some of it.
    float whiteBalance = 0.6f;
    /// The pipeline's volumetric medium (0 off): sunbeams through the window in a hazy room.
    float volumetricFog = 0.0f;
    float sunbeams = 0.12f;          ///< the room air's scattering per metre (0 off): sunbeams through the windows
    float sunbeamAnisotropy = 0.75f; ///< Henyey-Greenstein g: forward-biased, brightest looking toward the window
    int sunbeamSteps = 24;
    bool sunbeamHalfResolution = true;   ///< march at half size and upsample (a quarter of the cost)
    int sunbeamMotes = 400;          ///< dust motes drifting in the beams (0 off)
    bool steam = true;               ///< the plume over the cup on the coffee table
    float sunbeamMoteSize = 2.0f;    ///< a mote's diameter in pixels at a 540-line frame
    /// Depth of field as a thin lens on a 24 mm sensor: focal length (mm),
    /// f-number, the largest blur radius (screen fraction) and the focus
    /// distance (metres; 0 focuses on whatever is at the frame's centre).
    bool depthOfField = true;
    float dofFocalLength = 35.0f;
    float dofFNumber = 4.0f;
    float dofMaxRadius = 0.012f;
    float dofFocusDistance = 0.0f;
    int bloomIterations = 4;
    bool fxaa = true;
    bool ssao = true;
    /// CNA's SSAO radius is a fraction of the screen (UV units), and its depth
    /// bias (0.005) and range (a quarter of the radius) are fractions of the
    /// prepass far plane (R-32): at 40 m an occluder had to stand 0.2..0.4 m
    /// off a surface to count and a sofa cast no ambient shadow on the floor;
    /// 16 m brings the bias to 8 cm. The same plane bounds the depth of field
    /// and the autofocus (the street reads as 16 m, which the lens cannot tell
    /// from 20).
    float ssaoRadius = 0.04f;
    float ssaoIntensity = 1.0f;
    int ssaoSamples = 16;
    float prepassFarPlane = 16.0f;
    bool ssr = false;
    bool contactShadows = false;
    bool colourGrade = false;

    bool shadows = true;
    int shadowQuality = 3;      ///< 1 low .. 4 ultra
    int shadowCascades = 3;
    float shadowDistance = 45.0f;   ///< the terrace opposite stands at ~20 m; 45 keeps its shadows on the road
    float shadowSplitLambda = 0.75f;
    float shadowBlendBand = 1.0f;
    float shadowDepthBias = 0.006f;
    bool shadowDebugTint = false;
    bool shadowCasterCull = true;   ///< light-space caster culling per cascade (off to diagnose)

    bool imageBasedLighting = true;
    float iblIntensity = 1.0f;
    bool interiorProbes = true;
    int probeFaceSize = 64;
    /// A capture is one bounce; the irradiance is scaled up to stand in for
    /// the bounces that were not rendered.
    float probeBounceGain = 1.25f;
    /// Prefiltered specular in half-float cubes, one per roughness class,
    /// instead of CNA's 8-bit mip chain (off: CNA's product).
    bool specularClasses = true;
    bool sky = true;
    bool planarReflections = true;
    float reflectionScale = 0.5f;   ///< reflection target size relative to the frame
    bool wireframe = false;
    bool debugOverlay = false;
};

}  // namespace CnaRoom
