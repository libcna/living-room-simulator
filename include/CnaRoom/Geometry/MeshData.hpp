// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"

#include <cstdint>
#include <vector>

namespace CnaRoom::Geometry {

using Vertex = Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTexture;

/// CPU-side triangle mesh in the vertex format PbrEffect consumes.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool empty() const { return indices.empty(); }
    [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
    [[nodiscard]] Microsoft::Xna::Framework::BoundingBox bounds() const;
    void append(const MeshData& other);
    void clear()
    {
        vertices.clear();
        indices.clear();
    }
};

}  // namespace CnaRoom::Geometry
