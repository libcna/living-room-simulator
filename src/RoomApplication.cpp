// SPDX-License-Identifier: MIT
#include "CnaRoom/RoomApplication.hpp"

#include "CnaRoom/Assets/ModelLibrary.hpp"
#include "CnaRoom/Render/MaterialLibrary.hpp"
#include "CnaRoom/Render/SceneRenderer.hpp"
#include "CnaRoom/Scene/RoomScene.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "System/Diagnostics/Stopwatch.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;

namespace CnaRoom {

namespace {

bool parseCamera(const std::string& text, Vector3& position, float& yaw, float& pitch)
{
    float v[5];
    std::stringstream stream(text);
    for (float& f : v)
    {
        std::string part;
        if (!std::getline(stream, part, ',')) return false;
        f = std::strtof(part.c_str(), nullptr);
    }
    position = Vector3(v[0], v[1], v[2]);
    yaw = v[3];
    pitch = v[4];
    return true;
}

}  // namespace

RoomApplication::RoomApplication() : graphics_(std::make_unique<GraphicsDeviceManager>(this)) {}

RoomApplication::~RoomApplication() = default;

bool RoomApplication::configure(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "living-room-simulator: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h")
        {
            std::printf(
                "living-room-simulator %s -- a realistic living room rendered with CNA (%s)\n\n"
                "  --width W --height H      window size (default 1280x720)\n"
                "  --no-vsync                do not wait for vertical retrace\n"
                "  --frames N                draw N frames, then exit (prints frame statistics)\n"
                "  --screenshot PATH         write the last frame to PATH (PNG)\n"
                "  --record DIR[,EVERY]      write every EVERYth frame to DIR/frame-NNNN.png (deterministic 1/60 s steps)\n"
                "  --view NAME               start at a named viewpoint (entrance, sofa-to-tv,\n"
                "                            tv-to-sofa, bookshelf, window, window-close, material, corner)\n"
                "  --camera x,y,z,yaw,pitch  start at an explicit camera (degrees)\n"
                "  --time HH:MM              game clock at start (default 14:30); the sun follows it\n"
                "  --day-length MIN          real minutes per game day (default 24; 0 freezes the clock)\n"
                "  --latitude DEG            observer latitude (default 50.1), --day-of-year N (default 256)\n"
                "  --moon-age DAYS           days since new moon (default 10)\n"
                "  --pause                   start with the clock paused\n"
                "  --sun ELEV,AZIM           fix the sun (degrees) instead of following the clock\n"
                "  --weather KIND            clear|cloudy|overcast|rain|storm|snow|hail (default: evolves from cloudy)\n"
                "  --hold-weather            keep the weather kind (no automatic transitions)\n"
                "  --weather-speed S         transition speed multiplier (default 1)\n"
                "  --temperature C           fix the outside temperature (snow below 1 C)\n"
                "  --clouds COVER[,DENSITY]  fixed cloud coverage 0..1 and density (disables the weather system)\n"
                "  --haze H                  fixed horizon haze 0..1 (disables the weather system)\n"
                "  --exposure E              fixed tonemapper exposure\n"
                "  --exposure-mode M         auto (image-based, default) | analytic (from the lighting state)\n"
                "  --texture-size N          procedural texture resolution (default 1024)\n"
                "  --assets DIR              asset directory (default: the source tree assets/)\n"
                "  --no-shadows --no-ssao --no-bloom --no-fxaa --no-hdr --no-ibl --no-sky --no-probes\n"
                "  --probe-size N            interior probe face size (default 64), --probe-gain G bounce gain (1.5)\n"
                "  --lamps on|off|auto       artificial lights (auto: on when the sun is low)\n"
                "  --tv on|off|auto          television (auto: follows the lamps)\n"
                "  --shadow-quality Q        1 low .. 4 ultra (default 3)\n"
                "  --shadow-tint             tint the shadow cascades for inspection\n"
                "  --ssao-radius R           SSAO radius as a fraction of the screen (default 0.04)\n"
                "  --ssao-intensity I        SSAO strength (default 1.0), --ssao-samples N (default 16)\n"
                "  --prepass-far F           the depth prepass far plane in metres (default 16; SSAO's bias and range scale with it)\n"
                "  --texture-cache DIR|off   cache baked textures as PNG (default: <assets>/cache/textures)\n"
                "  --ssr                     enable screen-space reflections\n"
                "  --overlay                 show the statistics overlay\n"
                "  --grain G, --aberration A, --vignette V   lens look (defaults 0.015, 0.004, 0.12); --clean zeroes all three\n"
                "  --dof F,MM                depth of field: f-number and focal length (default 4,35); --no-dof turns it off\n"
                "  --flare I[,T]             lens flare ghosts: strength (default 0, off) and display-referred threshold (default 6)\n"
                "  --ev STOPS                exposure compensation on top of the adapted exposure (default 0)\n"
                "  --white-balance S         how much of the illuminant's cast the camera takes out, 0..1 (default 0.6)\n"
                "  --bloom-intensity I, --bloom-iterations N   bloom strength (default 0.08) and pyramid depth (default 4)\n"
                "  --focus D                 focus distance in metres (default: autofocus on the frame's centre)\n"
                "  --dump-depth FILE         write the first frame's prepass depth as a grey PNG (0..10 m, focus block marked)\n"
                "  --volumetric D            the pipeline's volumetric fog density (experimental, default 0)\n"
                "  --sunbeams D[,G,N]        sunlight scattered by the room's air: density per metre (default 0.12, 0 off),\n"
                "                            anisotropy (default 0.75) and march steps (default 24)\n"
                "  --sunbeams-full           march the beams at full size (the default marches at half size and upsamples)\n"
                "  --lamp-haze D             the pendant's light scattered by the room's air at night (default 0.35, 0 off)\n"
                "  --contact-shadows D       screen-space contact shadows toward the sun, 0..1 (default 0, experimental)\n"
                "  --no-decals               no wear and stains projected onto the room\n"
                "  --dolly TARGET SECONDS    glide the camera from its start to TARGET (x,y,z,yaw,pitch or a view name) over SECONDS\n"
                "  --motion-blur S           the pipeline's camera motion blur for moving shots (default 0)\n"
                "  --motes N[,PX]            dust motes drifting in the beams (default 400, 0 off) and their size in pixels at 540p (default 2)\n"
                "  --no-steam                no steam over the cup on the coffee table\n"
                "  --no-reflections          planar reflections off (mirror, television screen)\n"
                "  --no-specular-classes     CNA's 8-bit prefiltered specular instead of the half-float roughness classes\n"
                "  --reflection-scale S      reflection target size relative to the frame (default 0.5)\n"
                "  --model-texture-size N    halve imported model textures above N px (default 1024, 0 keeps them)\n"
                "  --log-every N             log frame statistics every N frames\n"
                "  --dump-probes DIR         write each baked probe's faces and irradiance as PNG strips\n",
                CNA_ROOM_VERSION, CNA_ROOM_RENDERER_NAME);
            return false;
        }
        else if (arg == "--width") settings_.windowWidth = std::atoi(next("--width"));
        else if (arg == "--height") settings_.windowHeight = std::atoi(next("--height"));
        else if (arg == "--no-vsync") settings_.vsync = false;
        else if (arg == "--frames") frameBudget_ = std::atoi(next("--frames"));
        else if (arg == "--screenshot") screenshotPath_ = next("--screenshot");
        else if (arg == "--record")
        {
            // DIR[,EVERY]: every EVERYth frame written to DIR/frame-NNNN.png in deterministic time.
            const std::string v = next("--record");
            const std::size_t comma = v.find(',');
            recordDirectory_ = v.substr(0, comma);
            recordEvery_ = comma == std::string::npos ? 1 : std::max(1, std::atoi(v.c_str() + comma + 1));
        }
        else if (arg == "--view") startView_ = next("--view");
        else if (arg == "--camera")
        {
            cameraOverride_ = parseCamera(next("--camera"), cameraOverridePosition_, cameraOverrideYaw_,
                                          cameraOverridePitch_);
            if (!cameraOverride_)
            {
                std::fprintf(stderr, "living-room-simulator: --camera wants x,y,z,yaw,pitch\n");
                std::exit(2);
            }
        }
        else if (arg == "--sun")
        {
            const std::string v = next("--sun");
            const auto comma = v.find(',');
            sunElevationDegrees_ = std::strtof(v.c_str(), nullptr);
            if (comma != std::string::npos) sunAzimuthDegrees_ = std::strtof(v.c_str() + comma + 1, nullptr);
            sunOverride_ = true;
        }
        else if (arg == "--time")
        {
            float hours = 0.0f;
            if (!TimeOfDay::parseClock(next("--time"), hours))
            {
                std::fprintf(stderr, "living-room-simulator: --time wants HH:MM\n");
                std::exit(2);
            }
            clock_.setHours(hours);
        }
        else if (arg == "--day-length") clock_.setSecondsPerDay(std::strtof(next("--day-length"), nullptr) * 60.0f);
        else if (arg == "--latitude") clock_.setLatitudeDegrees(std::strtof(next("--latitude"), nullptr));
        else if (arg == "--day-of-year") clock_.setDayOfYear(std::atoi(next("--day-of-year")));
        else if (arg == "--moon-age") clock_.setMoonAgeDays(std::strtof(next("--moon-age"), nullptr));
        else if (arg == "--pause") clock_.setPaused(true);
        else if (arg == "--clouds")
        {
            const std::string v = next("--clouds");
            const auto comma = v.find(',');
            cloudCoverage_ = std::strtof(v.c_str(), nullptr);
            if (comma != std::string::npos) cloudDensity_ = std::strtof(v.c_str() + comma + 1, nullptr);
            weatherForced_ = true;
        }
        else if (arg == "--weather")
        {
            WeatherKind kind = WeatherKind::Cloudy;
            if (!WeatherSystem::parse(next("--weather"), kind))
            {
                std::fprintf(stderr, "living-room-simulator: --weather wants clear|cloudy|overcast|rain|storm|snow|hail\n");
                std::exit(2);
            }
            weather_.setKind(kind, true);
        }
        else if (arg == "--hold-weather") weather_.setHold(true);
        else if (arg == "--weather-speed") weather_.setSpeed(std::strtof(next("--weather-speed"), nullptr));
        else if (arg == "--temperature") weather_.setTemperatureOverride(std::strtof(next("--temperature"), nullptr));
        else if (arg == "--haze")
        {
            haze_ = std::strtof(next("--haze"), nullptr);
            weatherForced_ = true;
        }
        else if (arg == "--exposure")
        {
            settings_.exposure = std::strtof(next("--exposure"), nullptr);
            exposureLocked_ = true;
            exposureMode_ = 0;
        }
        else if (arg == "--ev") exposureBias_ = std::clamp(std::strtof(next("--ev"), nullptr), -3.0f, 3.0f);
        else if (arg == "--exposure-mode")
        {
            const std::string v = next("--exposure-mode");
            if (v == "auto") exposureMode_ = 2;
            else if (v == "analytic") exposureMode_ = 1;
            else
            {
                std::fprintf(stderr, "living-room-simulator: --exposure-mode wants auto or analytic\n");
                std::exit(2);
            }
        }
        else if (arg == "--assets") assetDirectory_ = next("--assets");
        else if (arg == "--texture-size") textureSize_ = std::max(64, std::atoi(next("--texture-size")));
        else if (arg == "--model-texture-size") modelTextureCap_ = std::max(0, std::atoi(next("--model-texture-size")));
        else if (arg == "--no-shadows") settings_.shadows = false;
        else if (arg == "--no-ssao") settings_.ssao = false;
        else if (arg == "--no-bloom") settings_.bloom = false;
        else if (arg == "--grain") settings_.filmGrain = std::strtof(next("--grain"), nullptr);
        else if (arg == "--aberration") settings_.chromaticAberration = std::strtof(next("--aberration"), nullptr);
        else if (arg == "--vignette") settings_.vignette = std::strtof(next("--vignette"), nullptr);
        else if (arg == "--clean") settings_.filmGrain = settings_.chromaticAberration = settings_.vignette = settings_.lensFlare = 0.0f;
        else if (arg == "--flare")
        {
            const std::string v = next("--flare");
            settings_.lensFlare = std::max(0.0f, std::strtof(v.c_str(), nullptr));
            const auto comma = v.find(',');
            if (comma != std::string::npos) settings_.lensFlareThreshold = std::max(0.5f, std::strtof(v.c_str() + comma + 1, nullptr));
        }
        else if (arg == "--no-dof") settings_.depthOfField = false;
        else if (arg == "--prepass-far") settings_.prepassFarPlane = std::clamp(std::strtof(next("--prepass-far"), nullptr), 2.0f, 400.0f);
        else if (arg == "--white-balance") settings_.whiteBalance = std::clamp(std::strtof(next("--white-balance"), nullptr), 0.0f, 1.0f);
        else if (arg == "--bloom-intensity") settings_.bloomIntensity = std::max(0.0f, std::strtof(next("--bloom-intensity"), nullptr));
        else if (arg == "--bloom-iterations") settings_.bloomIterations = std::clamp(std::atoi(next("--bloom-iterations")), 1, 7);
        else if (arg == "--dof")
        {
            const std::string v = next("--dof");
            const auto comma = v.find(',');
            settings_.dofFNumber = std::max(0.7f, std::strtof(v.c_str(), nullptr));
            if (comma != std::string::npos) settings_.dofFocalLength = std::clamp(std::strtof(v.c_str() + comma + 1, nullptr), 8.0f, 400.0f);
        }
        else if (arg == "--focus") settings_.dofFocusDistance = std::strtof(next("--focus"), nullptr);
        else if (arg == "--volumetric") settings_.volumetricFog = std::strtof(next("--volumetric"), nullptr);
        else if (arg == "--sunbeams-full") settings_.sunbeamHalfResolution = false;
        else if (arg == "--lamp-haze") settings_.lampHaze = std::max(0.0f, std::strtof(next("--lamp-haze"), nullptr));
        else if (arg == "--contact-shadows") settings_.contactShadows = std::clamp(std::strtof(next("--contact-shadows"), nullptr), 0.0f, 1.0f);
        else if (arg == "--no-contact-shadows") settings_.contactShadows = 0.0f;
        else if (arg == "--no-decals") settings_.decals = false;
        else if (arg == "--dolly")
        {
            dollyTarget_ = next("--dolly");
            dollySeconds_ = std::max(0.01f, std::strtof(next("--dolly"), nullptr));
        }
        else if (arg == "--motion-blur") settings_.motionBlur = std::max(0.0f, std::strtof(next("--motion-blur"), nullptr));
        else if (arg == "--no-steam") settings_.steam = false;
        else if (arg == "--motes")
        {
            const std::string v = next("--motes");
            settings_.sunbeamMotes = std::clamp(std::atoi(v.c_str()), 0, 1200);
            const auto comma = v.find(',');
            if (comma != std::string::npos) settings_.sunbeamMoteSize = std::strtof(v.c_str() + comma + 1, nullptr);
        }
        else if (arg == "--sunbeams")
        {
            const std::string v = next("--sunbeams");
            settings_.sunbeams = std::strtof(v.c_str(), nullptr);
            const auto comma = v.find(',');
            if (comma != std::string::npos)
            {
                settings_.sunbeamAnisotropy = std::strtof(v.c_str() + comma + 1, nullptr);
                const auto second = v.find(',', comma + 1);
                if (second != std::string::npos) settings_.sunbeamSteps = std::atoi(v.c_str() + second + 1);
            }
        }
        else if (arg == "--no-fxaa") settings_.fxaa = false;
        else if (arg == "--no-hdr") settings_.hdr = false;
        else if (arg == "--no-ibl") settings_.imageBasedLighting = false;
        else if (arg == "--no-sky") settings_.sky = false;
        else if (arg == "--no-probes") settings_.interiorProbes = false;
        else if (arg == "--no-reflections") settings_.planarReflections = false;
        else if (arg == "--no-specular-classes") settings_.specularClasses = false;
        else if (arg == "--reflection-scale") settings_.reflectionScale = std::clamp(std::strtof(next("--reflection-scale"), nullptr), 0.1f, 1.0f);
        else if (arg == "--lamps")
        {
            const std::string v = next("--lamps");
            lampsMode_ = v == "on" ? 1 : v == "off" ? 0 : 2;
        }
        else if (arg == "--tv")
        {
            const std::string v = next("--tv");
            televisionMode_ = v == "on" ? 1 : v == "off" ? 0 : 2;
        }
        else if (arg == "--probe-size") settings_.probeFaceSize = std::atoi(next("--probe-size"));
        else if (arg == "--probe-gain") settings_.probeBounceGain = std::strtof(next("--probe-gain"), nullptr);
        else if (arg == "--shadow-quality") settings_.shadowQuality = std::clamp(std::atoi(next("--shadow-quality")), 1, 4);
        else if (arg == "--shadow-tint") settings_.shadowDebugTint = true;
        else if (arg == "--no-shadow-cull") settings_.shadowCasterCull = false;
        else if (arg == "--ssao-radius") settings_.ssaoRadius = std::strtof(next("--ssao-radius"), nullptr);
        else if (arg == "--ssao-intensity") settings_.ssaoIntensity = std::strtof(next("--ssao-intensity"), nullptr);
        else if (arg == "--ssao-samples") settings_.ssaoSamples = std::max(4, std::atoi(next("--ssao-samples")));
        else if (arg == "--texture-cache") textureCache_ = next("--texture-cache");
        else if (arg == "--ssr") settings_.ssr = true;
        else if (arg == "--overlay") settings_.debugOverlay = true;
        else if (arg == "--log-every") logEvery_ = std::atoi(next("--log-every"));
        else if (arg == "--dump-probes") probeDumpDirectory_ = next("--dump-probes");
        else if (arg == "--dump-depth") depthDumpPath_ = next("--dump-depth");
        else
        {
            std::fprintf(stderr, "living-room-simulator: unknown option '%s' (try --help)\n", arg.c_str());
            std::exit(2);
        }
    }
    return true;
}

void RoomApplication::Initialize()
{
    graphics_->setPreferredBackBufferWidthProperty(settings_.windowWidth);
    graphics_->setPreferredBackBufferHeightProperty(settings_.windowHeight);
    graphics_->setSynchronizeWithVerticalRetraceProperty(settings_.vsync);
    graphics_->setGraphicsProfileProperty(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
    graphics_->ApplyChanges();

    getWindowProperty().setTitleProperty("living-room-simulator -- a living room built with CNA");
    setIsMouseVisibleProperty(true);
    setIsFixedTimeStepProperty(false);

    Game::Initialize();
}

void RoomApplication::LoadContent()
{
    Game::LoadContent();
    GraphicsDevice& device = getGraphicsDeviceProperty();
    CNA::Logger::Info(std::string("living-room-simulator: renderer ") + CNA_ROOM_RENDERER_NAME);

    System::Diagnostics::Stopwatch watch = System::Diagnostics::Stopwatch::StartNew();
    materials_ = std::make_unique<MaterialLibrary>(device);
    if (textureCache_ != "off") materials_->setCacheDirectory(textureCache_.empty() ? assetDirectory_ + "/cache/textures" : textureCache_);
    materials_->buildProcedural(seed_, textureSize_);
    const double materialsMs = static_cast<double>(watch.getElapsedTicksProperty()) / 10000.0;

    const auto& viewport = device.getViewportProperty();
    const int width = viewport.getWidthProperty(), height = viewport.getHeightProperty();
    renderer_ = std::make_unique<SceneRenderer>(device);
    renderer_->initialise(settings_, width, height);
    if (!probeDumpDirectory_.empty()) renderer_->setProbeDumpDirectory(probeDumpDirectory_);
    if (!depthDumpPath_.empty()) renderer_->setDepthDumpPath(depthDumpPath_);

    getContentProperty().setRootDirectoryProperty(assetDirectory_);
    compiledContent_ = std::make_unique<Content::ContentManager>(&getServicesProperty(), assetDirectory_ + "/cnb");
    models_ = std::make_unique<ModelLibrary>(device, getContentProperty(), *materials_, compiledContent_.get());
    models_->setTextureCap(modelTextureCap_);
    scene_ = std::make_unique<RoomScene>(device, *materials_, *models_, *renderer_,
                                         assetDirectory_ + "/external/extracted");
    scene_->build();

    camera_.setPerspective(MathHelper::ToRadians(settings_.verticalFovDegrees),
                           static_cast<float>(width) / static_cast<float>(std::max(1, height)),
                           settings_.nearPlane, settings_.farPlane);
    if (cameraOverride_)
    {
        camera_.setPosition(cameraOverridePosition_);
        camera_.setOrientation(MathHelper::ToRadians(cameraOverrideYaw_), MathHelper::ToRadians(cameraOverridePitch_));
    }
    else
    {
        applyViewpoint(startView_);
    }
    if (!dollyTarget_.empty())
    {
        // The dolly's end: a camera spec, or a named viewpoint; its start is where the camera is now.
        Vector3 position;
        float yaw = 0.0f, pitch = 0.0f;
        bool found = parseCamera(dollyTarget_, position, yaw, pitch);
        if (!found)
            for (const Viewpoint& view : scene_->viewpoints())
                if (view.name == dollyTarget_)
                {
                    position = view.position;
                    yaw = view.yawDegrees;
                    pitch = view.pitchDegrees;
                    found = true;
                    break;
                }
        if (found)
        {
            dollyFromPosition_ = camera_.position();
            dollyFromYaw_ = camera_.yaw();
            dollyFromPitch_ = camera_.pitch();
            dollyToPosition_ = position;
            dollyToYaw_ = MathHelper::ToRadians(yaw);
            dollyToPitch_ = MathHelper::ToRadians(pitch);
            dollyElapsed_ = 0.0f;
            dollyActive_ = true;
            CNA::Logger::Info("living-room-simulator: dolly to " + dollyTarget_ + " over " + std::to_string(dollySeconds_) + " s");
        }
        else
            CNA::Logger::Warn("living-room-simulator: --dolly target not understood: " + dollyTarget_);
    }

    updateSky();
    {
        const bool low = renderer_->sky().lighting().sunElevation < MathHelper::ToRadians(3.0f);
        const bool lamps = lampsMode_ == 1 || (lampsMode_ == 2 && low);
        scene_->setLampsOn(lamps);
        scene_->setStreetLightsOn(lampsMode_ == 1 || renderer_->sky().lighting().sunElevation < MathHelper::ToRadians(1.5f));
        scene_->setTelevisionOn(televisionMode_ == 1 || (televisionMode_ == 2 && lamps));
        scene_->update(10.0f);   // start with the fades settled
    }
    updateWeather(0.0f);
    updateSky();
    if (!exposureLocked_)
    {
        exposureSmoothed_ = scheduledExposure();
        settings_.exposure = exposureSmoothed_ * std::exp2(exposureBias_);
    }
    renderer_->setAutoExposureEnabled(true);   // measured every frame; the mode decides whether it is used
    overlay_ = std::make_unique<Overlay>(device);
    renderer_->render(camera_, settings_);   // fits the shadow cascades the probe capture reads
    renderer_->bakeInteriorProbes(scene_->probePositions(), settings_);
    probeBakeSunY_ = renderer_->sky().state().sunDirection.Y;
    probeBakeLamps_ = scene_->lampsOn();
    contentLoaded_ = true;
    CNA::Logger::Info("living-room-simulator: clock " + clock_.clockText() + ", sun elevation "
                      + std::to_string(sunElevationDegrees_) + " deg, azimuth " + std::to_string(sunAzimuthDegrees_)
                      + " deg, exposure " + std::to_string(settings_.exposure) + ", lamps "
                      + (scene_->lampsOn() ? "on" : "off"));
    CNA::Logger::Info("living-room-simulator: content loaded in "
                      + std::to_string(static_cast<double>(watch.getElapsedTicksProperty()) / 10000.0)
                      + " ms (materials " + std::to_string(materialsMs) + " ms, " + std::to_string(models_->compiledLoads())
                      + " models from .cnb)");
}

void RoomApplication::applyViewpoint(const std::string& name)
{
    const Viewpoint* view = scene_ != nullptr ? scene_->viewpoint(name) : nullptr;
    if (view == nullptr)
    {
        CNA::Logger::Warn("living-room-simulator: no viewpoint named '" + name + "'; using the first");
        if (scene_ == nullptr || scene_->viewpoints().empty()) return;
        view = &scene_->viewpoints().front();
    }
    camera_.setPosition(view->position);
    camera_.setOrientation(MathHelper::ToRadians(view->yawDegrees), MathHelper::ToRadians(view->pitchDegrees));
    controller_.reset();
}

void RoomApplication::updateSky()
{
    if (renderer_ == nullptr) return;
    SkyState state = renderer_->sky().state();
    if (sunOverride_)
    {
        const float el = MathHelper::ToRadians(sunElevationDegrees_);
        const float az = MathHelper::ToRadians(sunAzimuthDegrees_);
        state.sunDirection = Vector3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
        state.moonDirection = Vector3(-state.sunDirection.X, -state.sunDirection.Y * 0.8f - 0.1f, -state.sunDirection.Z);
    }
    else
    {
        state.sunDirection = clock_.sunDirection();
        state.moonDirection = clock_.moonDirection();
        state.moonPhase = clock_.moonPhase();
        sunElevationDegrees_ = clock_.sunElevationDegrees();
        sunAzimuthDegrees_ = clock_.sunAzimuthDegrees();
    }
    state.turbidity = 2.4f + haze_ * 4.0f;
    state.intensity = skyIntensity_;
    state.cloudCoverage = cloudCoverage_;
    state.cloudDensity = cloudDensity_;
    state.haze = haze_;
    state.timeSeconds = elapsedSeconds_;
    const WeatherState& weather = weather_.state();
    state.lightning = weather.lightning;
    state.starVisibility = weather.starVisibility;
    state.windX = std::sin(weather.windDirection) * weather.windSpeed;
    state.windZ = std::cos(weather.windDirection) * weather.windSpeed;
    renderer_->sky().update(state);
}

void RoomApplication::handleHotkeys(const KeyboardState& keyboard, const KeyboardState& previous)
{
    const auto pressed = [&](Keys key) { return keyboard.IsKeyDown(key) && !previous.IsKeyDown(key); };
    if (pressed(Keys::R)) applyViewpoint(startView_);
    if (pressed(Keys::F1)) settings_.debugOverlay = !settings_.debugOverlay;
    if (pressed(Keys::F12)) captureScreenshot("living-room-simulator-" + std::to_string(framesDrawn_) + ".png");
    if (pressed(Keys::L))
    {
        lampsMode_ = scene_->lampsOn() ? 0 : 1;   // manual until restarted; the clock no longer switches them
        scene_->setLampsOn(!scene_->lampsOn());
        scene_->setStreetLightsOn(scene_->lampsOn());
        renderer_->requestProbeBake(scene_->probePositions(), settings_, 1);
        probeBakeLamps_ = scene_->lampsOn();
    }
    if (pressed(Keys::P)) clock_.setPaused(!clock_.paused());
    if (pressed(Keys::F2)) { weather_.cycleKind(); weatherForced_ = false; }
    if (pressed(Keys::F3)) weather_.setHold(!weather_.held());
    if (pressed(Keys::PageUp)) { clock_.jump(0.5f); sunOverride_ = false; }
    if (pressed(Keys::PageDown)) { clock_.jump(-0.5f); sunOverride_ = false; }
    if (pressed(Keys::Home)) { clock_.setHours(14.5f); sunOverride_ = false; }
    if (pressed(Keys::T)) scene_->setTelevisionOn(!scene_->televisionOn());
    const Keys numbers[] = {Keys::D1, Keys::D2, Keys::D3, Keys::D4, Keys::D5, Keys::D6, Keys::D7, Keys::D8};
    for (int i = 0; i < 8; ++i)
        if (pressed(numbers[i]) && scene_ != nullptr && i < static_cast<int>(scene_->viewpoints().size()))
            applyViewpoint(scene_->viewpoints()[static_cast<std::size_t>(i)].name);

    const float step = keyboard.IsKeyDown(Keys::LeftShift) ? 2.0f : 0.5f;
    bool sunMoved = false;
    if (keyboard.IsKeyDown(Keys::OemOpenBrackets)) { sunAzimuthDegrees_ -= step; sunMoved = true; }
    if (keyboard.IsKeyDown(Keys::OemCloseBrackets)) { sunAzimuthDegrees_ += step; sunMoved = true; }
    if (keyboard.IsKeyDown(Keys::OemMinus)) { sunElevationDegrees_ = std::max(-12.0f, sunElevationDegrees_ - step); sunMoved = true; }
    if (keyboard.IsKeyDown(Keys::OemPlus)) { sunElevationDegrees_ = std::min(88.0f, sunElevationDegrees_ + step); sunMoved = true; }
    if (keyboard.IsKeyDown(Keys::OemComma)) { cloudCoverage_ = std::max(0.0f, cloudCoverage_ - 0.01f); sunMoved = true; weatherForced_ = true; }
    if (keyboard.IsKeyDown(Keys::OemPeriod)) { cloudCoverage_ = std::min(1.0f, cloudCoverage_ + 0.01f); sunMoved = true; weatherForced_ = true; }
    if (sunMoved)
    {
        sunOverride_ = true;
        updateSky();
    }
}

void RoomApplication::Update(GameTime& gameTime)
{
    Game::Update(gameTime);
    const bool deterministic = frameBudget_ > 0 || !screenshotPath_.empty() || !recordDirectory_.empty();
    deterministic_ = deterministic;
    renderer_->setFocusPullRate(deterministic ? 200.0f : 6.0f);   // a capture settles its focus within a frame
    const float dt = deterministic ? 1.0f / 60.0f
                                   : static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    elapsedSeconds_ += dt;

    const KeyboardState keyboard = Keyboard::GetState();
    if (keyboard.IsKeyDown(Keys::Escape)) Exit();
    if (contentLoaded_)
    {
        handleHotkeys(keyboard, previousKeyboard_);
        controller_.update(camera_, keyboard, dt);
        if (dollyActive_)
        {
            // Eased glide; the yaw takes the short way round.
            dollyElapsed_ += dt;
            const float t = std::clamp(dollyElapsed_ / dollySeconds_, 0.0f, 1.0f);
            const float e = t * t * (3.0f - 2.0f * t);
            float yawDelta = dollyToYaw_ - dollyFromYaw_;
            while (yawDelta > MathHelper::Pi) yawDelta -= MathHelper::TwoPi;
            while (yawDelta < -MathHelper::Pi) yawDelta += MathHelper::TwoPi;
            camera_.setPosition(Vector3::Lerp(dollyFromPosition_, dollyToPosition_, e));
            camera_.setOrientation(dollyFromYaw_ + yawDelta * e, dollyFromPitch_ + (dollyToPitch_ - dollyFromPitch_) * e);
            if (t >= 1.0f) dollyActive_ = false;
        }
        renderer_->setFrameTime(dt, elapsedSeconds_);
        if (!sunOverride_) clock_.advance(dt);
        updateWeather(dt);
        // The sky follows the clock and the weather; cloud drift needs the
        // elapsed time even when the sun is still.
        updateSky();
        updateSchedules(dt);
    }
    previousKeyboard_ = keyboard;
}

float RoomApplication::scheduledExposure() const
{
    // A key value for the room: the sky's hemisphere average plus a fraction
    // of the direct sun (it lights only part of the room) plus the lamps.
    // Calibrated so a clear afternoon sits near 1.0 and a lamp-lit evening
    // near 60, the values that read well in the milestone screenshots.
    const SkyLighting& lighting = renderer_->sky().lighting();
    const auto luminance = [](const Vector3& c) { return 0.2126f * c.X + 0.7152f * c.Y + 0.0722f * c.Z; };
    float key = luminance(lighting.ambientColour) + luminance(lighting.sunColour) * 0.05f + luminance(lighting.moonColour) * 0.3f;
    if (scene_ != nullptr && scene_->lampsOn()) key += 0.010f;
    key = std::max(key, 0.00025f);   // a moonless night: what the eye can still use
    return std::clamp(0.5f / key, 0.6f, 2000.0f);
}

void RoomApplication::updateWeather(float dt)
{
    const float gameHours = sunOverride_ || clock_.paused() || clock_.secondsPerDay() <= 0.0f
                                ? 0.0f
                                : dt * (24.0f / clock_.secondsPerDay());
    weather_.advance(gameHours, dt, clock_.hours());
    renderer_->setProbeBakePace(gameHours * 60.0f);
    const WeatherState& w = weather_.state();
    if (!weatherForced_)
    {
        cloudCoverage_ = w.cloudCoverage;
        cloudDensity_ = w.cloudDensity;
        haze_ = w.haze;
    }
    scene_->applyWeather(w, elapsedSeconds_);
    scene_->setClockTime(clock_.hours());
    // The stove is lit on cool evenings: lamps on and the weather under 14 C.
    scene_->setFireOn(scene_->lampsOn() && w.temperatureC < 14.0f);

    // Aerial perspective: CNA's height fog with its daylight haze colour, so
    // only by day (the pass has no colour control from the pipeline) and
    // scaled by the weather's haze.
    {
        const float daylight = std::clamp(sunElevationDegrees_ / 6.0f, 0.0f, 1.0f);
        const float density = (0.0015f + 0.012f * w.haze + 0.02f * w.precipitation) * daylight;
        renderer_->setFog(density, 0.08f, -0.25f);
    }

    // Lightning as a directional flash from the strike's side of the sky.
    const Vector3 strike(std::sin(w.lightningAzimuth) * 0.6f, -0.75f, std::cos(w.lightningAzimuth) * 0.6f);
    renderer_->setLightning(w.lightning, strike);

    // Precipitation: over the garden and the street when the camera is in
    // the room, around the camera when it is outside.
    Precipitation::Params p;
    p.kind = w.type == PrecipitationType::Rain ? Precipitation::Kind::Rain
             : w.type == PrecipitationType::Snow ? Precipitation::Kind::Snow
             : w.type == PrecipitationType::Hail ? Precipitation::Kind::Hail : Precipitation::Kind::None;
    p.intensity = w.precipitation;
    p.wind = Vector3(std::sin(w.windDirection) * w.windSpeed, 0.0f, std::cos(w.windDirection) * w.windSpeed);
    const SkyLighting& lighting = renderer_->sky().lighting();
    p.radiance = lighting.ambientColour * 0.55f + Vector3(0.0004f, 0.00036f, 0.0003f) * (scene_->lampsOn() ? 1.0f : 0.0f);
    const RoomLayout& L = scene_->layout();
    const Vector3 eye = camera_.position();
    const bool inside = std::abs(eye.X) < L.halfWidth + 0.3f && std::abs(eye.Z) < L.halfDepth + 0.3f && eye.Y > -0.3f
                        && eye.Y < L.ceilingHeight + 0.3f;
    const float wallFace = -L.halfDepth - L.exteriorWallThickness;
    if (inside)
        p.volume = BoundingBox(Vector3(eye.X - 16.0f, -0.6f, wallFace - 26.0f), Vector3(eye.X + 16.0f, 11.0f, wallFace));
    else
        p.volume = BoundingBox(eye + Vector3(-14.0f, -6.0f, -14.0f), eye + Vector3(14.0f, 9.0f, 14.0f));
    p.timeSeconds = elapsedSeconds_;
    renderer_->setPrecipitation(p);
}

void RoomApplication::updateSchedules(float dt)
{
    // Lamps: on when the sun sinks below 1 degree, off once it climbs past 5
    // (the hysteresis keeps them from flickering around the horizon). Street
    // lights run on a photocell: on below 0.5, off above 3.
    if (lampsMode_ == 2)
    {
        const float elevation = sunElevationDegrees_;
        const bool on = scene_->lampsOn();
        if (!on && elevation < 1.0f) scene_->setLampsOn(true);
        else if (on && elevation > 5.0f) scene_->setLampsOn(false);
        if (televisionMode_ == 2 && scene_->televisionOn() != scene_->lampsOn())
            scene_->setTelevisionOn(scene_->lampsOn());
        const bool street = scene_->streetLightsOn();
        if (!street && elevation < 0.5f) scene_->setStreetLightsOn(true);
        else if (street && elevation > 3.0f) scene_->setStreetLightsOn(false);
    }
    scene_->update(dt);
    // Probes: re-capture when the lamps changed or the sun moved by ~3
    // degrees since the last capture (while it matters), one face per frame.
    if (!renderer_->probeBakePending())
    {
        const float sunY = renderer_->sky().state().sunDirection.Y;
        const bool lampsChanged = scene_->lampsOn() != probeBakeLamps_ && scene_->lampsSettled();
        const bool sunMoved = std::fabs(sunY - probeBakeSunY_) > 0.05f && (sunY > -0.15f || probeBakeSunY_ > -0.15f);
        if (lampsChanged || sunMoved)
        {
            renderer_->requestProbeBake(scene_->probePositions(), settings_, 1);
            probeBakeSunY_ = sunY;
            probeBakeLamps_ = scene_->lampsOn();
        }
    }
    // Exposure: the analytic schedule is the prior; when the image-based
    // measurement is available it decides within a factor of 1.8 of that
    // prior (a dark corner opens up, a sunlit wall pulls the exposure down; the
    // log-average barely notices a small bright pendant, so that view stays bright).
    if (!exposureLocked_)
    {
        const float prior = scheduledExposure();
        float target = prior;
        if (exposureMode_ == 2 && renderer_->measuredExposure() > 0.0f)
            // The meter may pull the schedule up to 2.5x either way: at night it
            // sits at the top of that band (a lamp-lit room reads brighter to a
            // meter than the schedule assumes), by day well inside it.
            target = std::clamp(renderer_->measuredExposure(), prior / 2.5f, prior * 2.5f);
        if (exposureSmoothed_ <= 0.0f) exposureSmoothed_ = target;
        // Interactive: 1.5/s adaptation. A fixed-frame capture has no time to
        // adapt, so it settles within its few frames instead.
        const float rate = 1.0f - std::exp(-dt * (deterministic_ ? 40.0f : 1.5f));
        exposureSmoothed_ = std::exp(std::log(exposureSmoothed_) + (std::log(target) - std::log(exposureSmoothed_)) * rate);
        settings_.exposure = exposureSmoothed_ * std::exp2(exposureBias_);
    }
}

void RoomApplication::Draw(const GameTime& gameTime)
{
    GraphicsDevice& device = getGraphicsDeviceProperty();
    if (!contentLoaded_)
    {
        device.Clear(Color::Black);
        Game::Draw(gameTime);
        return;
    }
    renderer_->render(camera_, settings_);
    if (settings_.debugOverlay && overlay_ != nullptr)
    {
        const SceneRenderer::Stats& s = renderer_->stats();
        char line[160];
        std::vector<std::string> lines;
        std::snprintf(line, sizeof(line), "%s  sun %.1f deg  %s", clock_.clockText().c_str(),
                      static_cast<double>(sunElevationDegrees_), weather_.describe().c_str());
        lines.emplace_back(line);
        std::snprintf(line, sizeof(line), "exposure %.2f (ev %+.1f, measured %.2f, lum %.4f, highlights %.0f%%)  lamps %s  tv %s",
                      static_cast<double>(settings_.exposure), static_cast<double>(exposureBias_), static_cast<double>(renderer_->measuredExposure()),
                      static_cast<double>(renderer_->measuredLuminance()), static_cast<double>(renderer_->highlightShare() * 100.0f),
                      scene_->lampsOn() ? "on" : "off", scene_->televisionOn() ? "on" : "off");
        lines.emplace_back(line);
        if (settings_.depthOfField)
        {
            const Vector3& wb = renderer_->whiteBalanceGain();
            std::snprintf(line, sizeof(line), "lens %.0f mm f/%.1f  focus %.2f m (%s%.2f)  wb gain %.2f %.2f %.2f", static_cast<double>(settings_.dofFocalLength),
                          static_cast<double>(settings_.dofFNumber), static_cast<double>(renderer_->focusDistance()),
                          settings_.dofFocusDistance > 0.0f ? "manual " : "auto, measured ", static_cast<double>(renderer_->measuredFocus()),
                          static_cast<double>(wb.X), static_cast<double>(wb.Y), static_cast<double>(wb.Z));
            lines.emplace_back(line);
        }
        std::snprintf(line, sizeof(line), "frame %.1f ms: shadow %.0f prepass %.0f refl %.0f (%d) opaque %.0f beams %.0f post %.0f  draws %d+%d  tris %zu  vis %d/%d",
                      static_cast<double>(s.frameMs), static_cast<double>(s.shadowMs), static_cast<double>(s.prepassMs),
                      static_cast<double>(s.reflectionMs), s.reflectionDrawCalls, static_cast<double>(s.opaqueMs), static_cast<double>(s.sunbeamMs),
                      static_cast<double>(s.postMs), s.drawCalls, s.shadowDrawCalls, s.triangles, s.visibleItems, s.totalItems);
        lines.emplace_back(line);
        if (s.gpuOpaqueMs >= 0.0)
        {
            std::snprintf(line, sizeof(line), "gpu: shadow %.0f prepass %.0f sky %.1f opaque %.0f beams %.0f post %.0f ms",
                          s.gpuShadowMs, s.gpuPrepassMs, s.gpuSkyMs, s.gpuOpaqueMs, s.gpuSunbeamMs, s.gpuPostMs);
            lines.emplace_back(line);
        }
        const Vector3 eye = camera_.position();
        std::snprintf(line, sizeof(line), "camera %.2f %.2f %.2f  yaw %.0f pitch %.0f  [F1 hide]", static_cast<double>(eye.X),
                      static_cast<double>(eye.Y), static_cast<double>(eye.Z), static_cast<double>(MathHelper::ToDegrees(camera_.yaw())),
                      static_cast<double>(MathHelper::ToDegrees(camera_.pitch())));
        lines.emplace_back(line);
        overlay_->draw(lines, 2);
    }
    Game::Draw(gameTime);

    ++framesDrawn_;
    if (!recordDirectory_.empty() && framesDrawn_ % recordEvery_ == 0)
    {
        if (framesDrawn_ == recordEvery_) std::filesystem::create_directories(recordDirectory_);
        char name[32];
        std::snprintf(name, sizeof name, "/frame-%04d.png", framesDrawn_ / recordEvery_);
        captureScreenshot(recordDirectory_ + name);
    }
    if (logEvery_ > 0 && framesDrawn_ % logEvery_ == 0 && framesDrawn_ != frameBudget_) logFrameStats();
    const bool last = frameBudget_ > 0 ? framesDrawn_ >= frameBudget_ : framesDrawn_ >= 3;
    if (!screenshotPath_.empty() && last)
    {
        captureScreenshot(screenshotPath_);
        screenshotPath_.clear();
        if (frameBudget_ == 0)
        {
            logFrameStats();
            Exit();
        }
    }
    if (frameBudget_ > 0 && framesDrawn_ >= frameBudget_)
    {
        logFrameStats();
        Exit();
    }
}

void RoomApplication::logFrameStats()
{
    const SceneRenderer::Stats& s = renderer_->stats();
    std::ostringstream out;
    out << "living-room-simulator: frame " << framesDrawn_ << " -- " << s.frameMs << " ms CPU (cull " << s.cullMs
        << ", shadow " << s.shadowMs << ", prepass " << s.prepassMs << ", reflection " << s.reflectionMs << " (" << s.reflectionDrawCalls
        << " draws), sky " << s.skyMs << ", opaque "
        << s.opaqueMs << ", beams " << s.sunbeamMs << ", post " << s.postMs << "); draws " << s.drawCalls << " (+" << s.shadowDrawCalls
        << " shadow), triangles " << s.triangles << ", visible " << s.visibleItems << "/" << s.totalItems
        << ", post passes " << s.postPasses << (s.usedSceneTarget ? ", HDR target" : ", back buffer")
        << "; clock " << clock_.clockText() << " sun " << sunElevationDegrees_ << " deg, focus " << renderer_->focusDistance()
        << " m, exposure " << settings_.exposure
        << " (measured " << renderer_->measuredExposure() << ", luminance " << renderer_->measuredLuminance() << " weighted / "
        << renderer_->plainLuminance() << " plain, highlights " << renderer_->highlightShare() << ")"
        << ", lamps " << (scene_->lampsOn() ? "on" : "off") << (renderer_->probeBakePending() ? ", probe bake pending" : "")
        << "; weather " << weather_.describe();
    if (s.gpuOpaqueMs >= 0.0)
        out << "; GPU shadow " << s.gpuShadowMs << " prepass " << s.gpuPrepassMs << " sky " << s.gpuSkyMs
            << " opaque " << s.gpuOpaqueMs << " beams " << s.gpuSunbeamMs << " post " << s.gpuPostMs << " ms";
    CNA::Logger::Info(out.str());
    for (const std::string& limitation : renderer_->limitations())
        CNA::Logger::Warn("living-room-simulator: limitation -- " + limitation);
}

void RoomApplication::captureScreenshot(const std::string& path)
{
    GraphicsDevice& device = getGraphicsDeviceProperty();
    try
    {
        const auto& viewport = device.getViewportProperty();
        const int width = viewport.getWidthProperty();
        const int height = viewport.getHeightProperty();
        const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::vector<Color> pixels(count, Color::Transparent);
        device.GetBackBufferData(pixels.data(), static_cast<int>(count));
        std::vector<std::uint8_t> rgba(count * 4u);
        for (std::size_t i = 0; i < count; ++i)
        {
            rgba[i * 4 + 0] = static_cast<std::uint8_t>(pixels[i].getRProperty());
            rgba[i * 4 + 1] = static_cast<std::uint8_t>(pixels[i].getGProperty());
            rgba[i * 4 + 2] = static_cast<std::uint8_t>(pixels[i].getBProperty());
            rgba[i * 4 + 3] = 255;
        }
        Texture2D shot = Texture2D::CreateFromPixels(device, width, height, rgba);
        shot.SaveAsPng(path);
        CNA::Logger::Info("living-room-simulator: wrote " + path);
    }
    catch (const System::NotSupportedException&)
    {
        CNA::Logger::Error("living-room-simulator: this renderer cannot read the back buffer, so '" + path
                           + "' was not written");
    }
}

}  // namespace CnaRoom
