// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Assets/ModelLibrary.hpp"
#include "CnaRoom/Scene/RoomLayout.hpp"
#include "CnaRoom/Geometry/MeshData.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
}

namespace CnaRoom {

class GpuMesh;
class MaterialLibrary;
class ModelLibrary;
class SceneRenderer;
struct ImportedModel;
struct Material;
struct WeatherState;

/// A named camera position used by --view and the number keys.
struct Viewpoint
{
    std::string name;
    Microsoft::Xna::Framework::Vector3 position;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
};

/**
 * @brief Builds the room: architecture now, furniture and props as they land.
 *
 * Owns the GPU meshes it creates; the renderer receives borrowed pointers.
 */
class RoomScene
{
public:
    RoomScene(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
              MaterialLibrary& materials, ModelLibrary& models, SceneRenderer& renderer,
              std::string extractedAssetDirectory);
    ~RoomScene();
    RoomScene(const RoomScene&) = delete;
    RoomScene& operator=(const RoomScene&) = delete;

    void build();

    [[nodiscard]] const RoomLayout& layout() const { return layout_; }
    [[nodiscard]] const std::vector<Viewpoint>& viewpoints() const { return viewpoints_; }
    [[nodiscard]] const Viewpoint* viewpoint(const std::string& name) const;
    [[nodiscard]] std::size_t meshCount() const { return meshes_.size(); }
    [[nodiscard]] std::size_t geometryBytes() const;

private:
    /// Uploads a mesh and registers it with the renderer under a material.
    const GpuMesh* place(Geometry::MeshData mesh, const std::string& materialName,
                         const std::string& name,
                         const Microsoft::Xna::Framework::Matrix& world =
                             Microsoft::Xna::Framework::Matrix::getIdentityProperty(),
                         bool castsShadow = true, bool exterior = false, bool shadowOnly = false);
public:
    /// Positions the interior environment probes are captured from.
    [[nodiscard]] std::vector<Microsoft::Xna::Framework::Vector3> probePositions() const;
    /// Switches the room's lamps (and their shades' glow) on or off; they fade over a second.
    void setLampsOn(bool on);
    /// Street lights and the lit windows opposite, on their own schedule.
    void setStreetLightsOn(bool on);
    void setTelevisionOn(bool on);
    /// Advances the lamp fades; call once per frame.
    void update(float dt);
    [[nodiscard]] bool streetLightsOn() const { return streetLightsOn_; }
    /// True once the lamp fade has reached its target (probes are re-captured then).
    [[nodiscard]] bool lampsSettled() const { return lampLevel_ == (lampsOn_ ? 1.0f : 0.0f); }
    /// Wet and snow-covered exterior surfaces, droplets on the panes, curtains swaying in the wind.
    void applyWeather(const WeatherState& weather, float seconds);
    /// Turns the wall clock's hands to the scene's time of day.
    void setClockTime(float hours);
    /// Lights or lets die the fire in the wood stove (it catches over seconds).
    void setFireOn(bool on);
    [[nodiscard]] bool fireOn() const { return fireOn_; }
    [[nodiscard]] bool lampsOn() const { return lampsOn_; }
    [[nodiscard]] bool televisionOn() const { return televisionOn_; }
private:

    void buildFloorAndCeiling();
    void buildWalls();
    void buildWindows();
    void buildDoor();
    void buildHallway();   ///< the space beyond the door, seen through it ajar
    void buildTrim();
    void buildRadiator();
    void buildFixtures();
    void buildExterior();
    void buildFurniture();
    void buildBookcase();
    void buildLamps();
    void buildViewpoints();

    /// Places an imported model by name: position is the footprint centre on
    /// the supporting surface, yaw in degrees, fitHeight scales uniformly when > 0.
    const ImportedModel* placeModel(const std::string& model,
                                    const Microsoft::Xna::Framework::Vector3& position,
                                    float yawDegrees, float fitHeight = 0.0f, bool castsShadow = true,
                                    const ModelLibrary::MaterialTweak& tweak = {});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    MaterialLibrary& materials_;
    ModelLibrary& models_;
    SceneRenderer& renderer_;
    int tvWallReflection_ = -1;
    int windowReflection_ = -1;
    int streetReflection_ = -1;   ///< the wet road and pavement (exterior-only capture)
    float windowGlassZ_ = 0.0f, windowGlassX0_ = 1e9f, windowGlassX1_ = -1e9f, windowGlassY0_ = 0.0f, windowGlassY1_ = 0.0f;
    std::string extractedDirectory_;
    int placedModels_ = 0;
    bool lampsOn_ = false;
    bool streetLightsOn_ = false;
    bool televisionOn_ = false;
    bool loggedTelevisionGlow_ = false;
    bool loggedWindowLights_ = false;
    float lampLevel_ = 0.0f;      ///< 0..1 fade of the room's lamps
    float streetLevel_ = 0.0f;
    void applyLampLevels();
    /// A material that glows when its lamp is on: name, colour, strength.
    struct Glow
    {
        std::string material;
        Microsoft::Xna::Framework::Vector3 colour;
        float strength;
        bool television = false;
        bool street = false;
    };
    std::vector<Glow> glows_;
    std::vector<Glow> extraGlows_;   ///< glows found while placing models (added to glows_ by buildLamps)
    /// A material whose look changes with wetness/snow: the dry values it started with.
    struct WeatherMaterial
    {
        std::string name;
        float wetDarken;      ///< base colour multiplier at full wetness (1 = none)
        float wetRoughness;   ///< roughness multiplier at full wetness
        float snowBlend;      ///< how much snow cover whitens it
        Microsoft::Xna::Framework::Vector3 dryColour;
        float dryRoughness = 1.0f;
        Microsoft::Xna::Framework::Graphics::Texture2D* dryAlbedo = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* dryNormal = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* dryOrm = nullptr;
        Microsoft::Xna::Framework::Vector2 dryUvScale{1.0f, 1.0f};
        bool snowed = false;
    };
    std::vector<WeatherMaterial> weatherMaterials_;
    struct Curtain
    {
        std::size_t item;
        Microsoft::Xna::Framework::Matrix baseWorld;
        Microsoft::Xna::Framework::Vector3 pivot;   ///< a point on the rod
        float phase;
    };
    std::vector<Curtain> curtains_;
    struct Tree
    {
        std::size_t canopy;                          ///< the crown's item
        std::size_t shadow;                          ///< its shadow proxies' item, or npos
        Microsoft::Xna::Framework::Vector3 pivot;    ///< where the boughs leave the trunk
        float phase;
    };
    std::vector<Tree> trees_;   ///< the street trees, swayed by the wind (applyWeather)
    struct FireCard
    {
        std::size_t item;
        Microsoft::Xna::Framework::Matrix baseWorld;
        float phase;
        int group = 0;          ///< 0 the stove (follows the fire), 1 the candle (follows the lamps)
        float strength = 0.9f;  ///< emissive at full, scene units
    };
    std::vector<FireCard> fireCards_;
    std::size_t fireEmbers_ = static_cast<std::size_t>(-1);
    bool fireOn_ = false;
    float fireLevel_ = 0.0f;
    float fireFlicker_ = 1.0f;
    float candleFlicker_ = 1.0f;
    float fireSeconds_ = 0.0f;
    void updateFire();
    void updateTelevisionGlow();   ///< the set's lamp from its picture's mean, each frame it plays
    void updateWindowLights();     ///< the windows' spots from the sky's light, when it changes
    std::size_t clockHourHand_ = static_cast<std::size_t>(-1), clockMinuteHand_ = static_cast<std::size_t>(-1);
    Microsoft::Xna::Framework::Vector3 clockHourPivot_, clockMinutePivot_;
    RoomLayout layout_;
    std::vector<Microsoft::Xna::Framework::Vector3> streetLampPositions_;
    std::vector<Microsoft::Xna::Framework::Vector3> chimneyTops_;   ///< the pots, for the smoke on cold days
    std::size_t smokePlumeCount_ = 0;
    std::vector<std::unique_ptr<GpuMesh>> meshes_;
    std::vector<Viewpoint> viewpoints_;
};

}  // namespace CnaRoom
