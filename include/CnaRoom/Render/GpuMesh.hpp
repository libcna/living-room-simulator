// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Geometry/MeshData.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class IndexBuffer;
    class ModelMeshPart;
    class VertexBuffer;
}

namespace CnaRoom {

/**
 * @brief A triangle mesh resident on the GPU, drawable in one call.
 *
 * Either owns its buffers (built from MeshData) or borrows a ModelMeshPart's
 * buffers from an imported model. Bounds are local-space.
 */
class GpuMesh
{
public:
    GpuMesh(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            const Geometry::MeshData& data, std::string name);
    GpuMesh(Microsoft::Xna::Framework::Graphics::ModelMeshPart& part,
            const Microsoft::Xna::Framework::BoundingBox& bounds, std::string name);
    ~GpuMesh();
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;

    void draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;

    [[nodiscard]] Microsoft::Xna::Framework::Graphics::ModelMeshPart* part() const { return part_; }
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingBox& bounds() const { return bounds_; }
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingSphere& sphere() const { return sphere_; }
    [[nodiscard]] int triangleCount() const { return triangleCount_; }
    [[nodiscard]] int vertexCount() const { return vertexCount_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] std::size_t gpuBytes() const { return gpuBytes_; }

private:
    std::string name_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> ownedVertexBuffer_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> ownedIndexBuffer_;
    Microsoft::Xna::Framework::Graphics::VertexBuffer* vertexBuffer_ = nullptr;
    Microsoft::Xna::Framework::Graphics::IndexBuffer* indexBuffer_ = nullptr;
    int vertexOffset_ = 0;
    int startIndex_ = 0;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ModelMeshPart> ownedPart_;
    Microsoft::Xna::Framework::Graphics::ModelMeshPart* part_ = nullptr;
    Microsoft::Xna::Framework::BoundingBox bounds_;
    Microsoft::Xna::Framework::BoundingSphere sphere_;
    int triangleCount_ = 0;
    int vertexCount_ = 0;
    std::size_t gpuBytes_ = 0;
};

}  // namespace CnaRoom
