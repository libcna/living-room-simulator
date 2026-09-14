// SPDX-License-Identifier: MIT
#pragma once

namespace CnaRoom {

/**
 * @brief The room's dimensions in metres, shared by the builders and the placers.
 *
 * Origin at the centre of the floor, Y up. The window wall is the -Z wall, the
 * door is in the +X wall, the television stands against the +Z wall and the
 * bookcase against the -X wall.
 */
struct RoomLayout
{
    float halfWidth = 3.10f;       ///< X extent of the interior (6.2 m wide)
    float halfDepth = 2.30f;       ///< Z extent of the interior (4.6 m deep)
    float ceilingHeight = 2.70f;
    float exteriorWallThickness = 0.30f;
    float interiorWallThickness = 0.12f;
    float floorThickness = 0.25f;

    // Windows in the -Z wall.
    int windowCount = 2;
    float windowCentreX[2] = {-1.45f, 1.45f};
    float windowWidth = 1.40f;
    float windowHeight = 1.50f;
    float windowSillHeight = 0.90f;
    float windowFrameDepth = 0.06f;   ///< frame profile thickness (into the wall)
    float windowFrameWidth = 0.07f;   ///< frame profile face width
    float windowRecessFromInside = 0.20f;  ///< where the frame sits inside the 0.30 wall

    // Door in the +X wall.
    float doorCentreZ = 1.30f;
    float doorWidth = 0.90f;
    float doorHeight = 2.05f;
    float doorFrameWidth = 0.08f;
    float doorFrameDepth = 0.12f;

    float baseboardHeight = 0.10f;
    float baseboardDepth = 0.014f;
    float corniceSize = 0.05f;

    [[nodiscard]] float windowTop() const { return windowSillHeight + windowHeight; }
};

}  // namespace CnaRoom
