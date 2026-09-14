// SPDX-License-Identifier: MIT
#include "CnaRoom/Assets/ModelLibrary.hpp"

#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include "CnaRoom/Assets/Image.hpp"
#include "CnaRoom/Assets/TextureBaker.hpp"
#include "CnaRoom/Render/GpuMesh.hpp"
#include "CnaRoom/Render/MaterialLibrary.hpp"
#include "CnaRoom/Render/SceneRenderer.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaRoom {

namespace {

Matrix absoluteTransform(const ModelMesh& mesh)
{
    Matrix world = Matrix::getIdentityProperty();
    for (const ModelBone* bone = mesh.getParentBoneProperty(); bone != nullptr;
         bone = bone->getParentProperty())
        world = world * bone->getTransformProperty();
    return world;
}

/// Measures a part's box from its own vertex positions (CNA's ModelMesh only
/// publishes a sphere; cna-street CNA-F18).
bool partBounds(ModelMeshPart& part, BoundingBox& out)
{
    VertexBuffer* buffer = part.getVertexBufferProperty();
    if (buffer == nullptr) return false;
    const int count = part.getNumVerticesProperty();
    if (count <= 0) return false;
    const VertexDeclaration& declaration = buffer->getVertexDeclarationProperty();
    const int stride = declaration.getVertexStrideProperty();
    if (stride <= 0) return false;
    int offset = -1;
    for (const VertexElement& element : declaration.GetVertexElements())
        if (element.getVertexElementUsageProperty() == VertexElementUsage::Position
            && element.getUsageIndexProperty() == 0
            && element.getVertexElementFormatProperty() == VertexElementFormat::Vector3)
            offset = element.getOffsetProperty();
    if (offset < 0 || offset + 12 > stride) return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count) * static_cast<std::size_t>(stride));
    try
    {
        buffer->GetDataRawEXT(part.getVertexOffsetProperty() * stride, bytes.data(), count, stride);
    }
    catch (const std::exception&)
    {
        return false;
    }
    Vector3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < count; ++i)
    {
        float p[3];
        std::memcpy(p, bytes.data() + static_cast<std::size_t>(i) * static_cast<std::size_t>(stride)
                           + static_cast<std::size_t>(offset), sizeof(p));
        const Vector3 v(p[0], p[1], p[2]);
        lo = Vector3::Min(lo, v);
        hi = Vector3::Max(hi, v);
    }
    out = BoundingBox(lo, hi);
    return true;
}


/// Replaces tangents that are missing, NaN or parallel to the normal. CNA's
/// importer derives tangents from the UV gradient and falls back to (1, 0, 0)
/// when a triangle's UVs are degenerate (palette-textured meshes map every
/// vertex to one texel); a fallback parallel to the normal makes the shader's
/// Gram-Schmidt step normalise a zero vector, and the NaN normal that follows
/// turns the surface into a full-strength mirror of the environment.
/// Returns the number of vertices repaired.
/// Mean roughness (ORM green) a part samples: its vertices' UVs looked up in
/// the map (a palette part sits on one texel; a photo-textured part averages
/// a few dozen of its own).
float partRoughnessMean(ModelMeshPart& part, const CnaRoom::Assets::Image& orm, const Vector2& uvScale, const Vector2& uvOffset)
{
    VertexBuffer* buffer = part.getVertexBufferProperty();
    if (buffer == nullptr || orm.width <= 0 || orm.height <= 0) return 1.0f;
    const int count = part.getNumVerticesProperty();
    if (count <= 0) return 1.0f;
    const VertexDeclaration& declaration = buffer->getVertexDeclarationProperty();
    const int stride = declaration.getVertexStrideProperty();
    int offset = -1;
    for (const VertexElement& element : declaration.GetVertexElements())
        if (element.getVertexElementUsageProperty() == VertexElementUsage::TextureCoordinate && element.getUsageIndexProperty() == 0
            && element.getVertexElementFormatProperty() == VertexElementFormat::Vector2)
            offset = element.getOffsetProperty();
    if (stride <= 0 || offset < 0 || offset + 8 > stride) return 1.0f;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count) * static_cast<std::size_t>(stride));
    try
    {
        buffer->GetDataRawEXT(part.getVertexOffsetProperty() * stride, bytes.data(), count, stride);
    }
    catch (const std::exception&)
    {
        return 1.0f;
    }
    const int step = std::max(1, count / 64);
    double sum = 0.0;
    int n = 0;
    for (int i = 0; i < count; i += step)
    {
        float uv[2];
        std::memcpy(uv, bytes.data() + static_cast<std::size_t>(i) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(offset), sizeof(uv));
        const float u = uv[0] * uvScale.X + uvOffset.X, v = uv[1] * uvScale.Y + uvOffset.Y;
        const float fu = u - std::floor(u), fv = v - std::floor(v);
        const int x = std::clamp(static_cast<int>(fu * static_cast<float>(orm.width)), 0, orm.width - 1);
        const int y = std::clamp(static_cast<int>(fv * static_cast<float>(orm.height)), 0, orm.height - 1);
        sum += orm.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(orm.width) + static_cast<std::size_t>(x)) * 4u + 1u];
        ++n;
    }
    return n > 0 ? static_cast<float>(sum / static_cast<double>(n) / 255.0) : 1.0f;
}

int repairTangents(VertexBuffer& buffer)
{
    const VertexDeclaration& declaration = buffer.getVertexDeclarationProperty();
    const int stride = declaration.getVertexStrideProperty();
    const int count = buffer.getVertexCountProperty();
    if (stride <= 0 || count <= 0) return 0;
    int normalOffset = -1, tangentOffset = -1;
    bool tangentHasW = false;
    for (const VertexElement& element : declaration.GetVertexElements())
    {
        if (element.getUsageIndexProperty() != 0) continue;
        const VertexElementUsage usage = element.getVertexElementUsageProperty();
        const VertexElementFormat format = element.getVertexElementFormatProperty();
        if (usage == VertexElementUsage::Normal && format == VertexElementFormat::Vector3)
            normalOffset = element.getOffsetProperty();
        else if (usage == VertexElementUsage::Tangent
                 && (format == VertexElementFormat::Vector3 || format == VertexElementFormat::Vector4))
        {
            tangentOffset = element.getOffsetProperty();
            tangentHasW = format == VertexElementFormat::Vector4;
        }
    }
    if (normalOffset < 0 || tangentOffset < 0) return 0;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count) * static_cast<std::size_t>(stride));
    try
    {
        buffer.GetDataRawEXT(0, bytes.data(), count, stride);
    }
    catch (const std::exception&)
    {
        return 0;
    }
    int repaired = 0;
    for (int i = 0; i < count; ++i)
    {
        std::uint8_t* vertex = bytes.data() + static_cast<std::size_t>(i) * static_cast<std::size_t>(stride);
        float n[3], t[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        std::memcpy(n, vertex + normalOffset, sizeof(n));
        std::memcpy(t, vertex + tangentOffset, tangentHasW ? 16u : 12u);
        const Vector3 normal(n[0], n[1], n[2]);
        const Vector3 tangent(t[0], t[1], t[2]);
        const Vector3 ortho = tangent - normal * Vector3::Dot(normal, tangent);
        const float length = ortho.Length();
        const bool bad = !(length > 1e-3f) || !std::isfinite(length) || !std::isfinite(t[3]);
        if (!bad) continue;
        const Vector3 axis = std::abs(normal.X) < 0.9f ? Vector3::UnitX : Vector3::UnitY;
        Vector3 fresh = Vector3::Cross(normal, axis);
        const float fl = fresh.Length();
        fresh = fl > 1e-6f ? fresh * (1.0f / fl) : Vector3::UnitZ;
        t[0] = fresh.X; t[1] = fresh.Y; t[2] = fresh.Z;
        if (!std::isfinite(t[3]) || t[3] == 0.0f) t[3] = 1.0f;
        std::memcpy(vertex + tangentOffset, t, tangentHasW ? 16u : 12u);
        ++repaired;
    }
    if (repaired > 0) buffer.SetDataRaw(bytes.data(), count, stride);
    return repaired;
}

BoundingBox transformBox(const BoundingBox& box, const Matrix& m)
{
    Vector3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; ++i)
    {
        const Vector3 corner((i & 1) ? box.Max.X : box.Min.X, (i & 2) ? box.Max.Y : box.Min.Y,
                             (i & 4) ? box.Max.Z : box.Min.Z);
        const Vector3 w = Vector3::Transform(corner, m);
        lo = Vector3::Min(lo, w);
        hi = Vector3::Max(hi, w);
    }
    return BoundingBox(lo, hi);
}

std::string fmt(float v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(v));
    return buffer;
}

}  // namespace

ModelLibrary::ModelLibrary(GraphicsDevice& device, Content::ContentManager& content, MaterialLibrary& materials,
                           Content::ContentManager* compiled)
    : device_(device), content_(content), materials_(materials), compiled_(compiled)
{
}

ModelLibrary::~ModelLibrary() = default;

Texture2D* ModelLibrary::remip(Texture2D* source, bool srgb)
{
    if (source == nullptr) return nullptr;
    const auto known = remipped_.find(source);
    if (known != remipped_.end()) return known->second;
    Texture2D* result = source;
    try
    {
        const int w = source->getWidthProperty(), h = source->getHeightProperty();
        // A compiled model hands every part its own copy of a shared texture
        // (and a glTF can repeat one too): a content signature read from the
        // GPU copy (size, two 16 x 16 blocks, sixteen full rows and sixteen
        // full columns spread over the image) finds the copy already rebuilt
        // for a few percent of the readback. Two blocks alone were not
        // enough: emissive maps that are black but for a bulb collided with
        // each other and with black ones of the same size, and a lamp lost
        // its light.
        if (w >= 16 && h >= 16)
        {
            const auto signStart = std::chrono::steady_clock::now();
            std::vector<Color> samples(static_cast<std::size_t>(std::max({w, h, 256})));
            std::uint64_t hash = std::uint64_t{1469598103934665603u} ^ (static_cast<std::uint64_t>(w) << 32u) ^ static_cast<std::uint64_t>(h)
                                 ^ (srgb ? std::uint64_t{0x9e3779b97f4a7c15u} : std::uint64_t{0});
            const auto fold = [&hash, &samples](int count) {
                for (int i = 0; i < count; ++i)
                {
                    hash ^= static_cast<std::uint64_t>(samples[static_cast<std::size_t>(i)].getPackedValueProperty());
                    hash *= std::uint64_t{1099511628211u};
                }
            };
            const Rectangle corner(0, 0, 16, 16), centre(w / 2 - 8, h / 2 - 8, 16, 16);
            source->GetData(0, &corner, samples.data(), 0, 256);
            fold(256);
            source->GetData(0, &centre, samples.data(), 0, 256);
            fold(256);
            constexpr int kStripes = 16;
            for (int i = 0; i < kStripes; ++i)
            {
                const Rectangle row(0, (h * (2 * i + 1)) / (2 * kStripes), w, 1);
                source->GetData(0, &row, samples.data(), 0, w);
                fold(w);
                const Rectangle column((w * (2 * i + 1)) / (2 * kStripes), 0, 1, h);
                source->GetData(0, &column, samples.data(), 0, h);
                fold(h);
            }
            signatureMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - signStart).count();
            const auto twin = signatures_.find(hash);
            if (twin != signatures_.end())
            {
                remipped_[source] = twin->second;
                ++dedupedTextures_;
                return twin->second;
            }
            signatures_[hash] = nullptr;   // filled below once rebuilt
            pendingSignature_ = hash;
        }
        if (w > 1 || h > 1)
        {
            const auto readStart = std::chrono::steady_clock::now();
            std::vector<Color> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
            source->GetData(0, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
            Assets::Image image(w, h);
            for (std::size_t i = 0; i < pixels.size(); ++i)
            {
                image.rgba[i * 4] = static_cast<std::uint8_t>(pixels[i].getRProperty());
                image.rgba[i * 4 + 1] = static_cast<std::uint8_t>(pixels[i].getGProperty());
                image.rgba[i * 4 + 2] = static_cast<std::uint8_t>(pixels[i].getBProperty());
                image.rgba[i * 4 + 3] = static_cast<std::uint8_t>(pixels[i].getAProperty());
            }
            readbackMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readStart).count();
            // Imported textures above the cap are halved (in linear light)
            // before the chain is built: a 4k map on a teacup costs the load
            // and the memory of sixteen 1k ones and shows none of it at 720p.
            const auto buildStart = std::chrono::steady_clock::now();
            while (textureCap_ > 0 && (image.width > textureCap_ || image.height > textureCap_) && image.width > 1 && image.height > 1)
                image = image.halved(srgb);
            result = materials_.own(Assets::TextureBaker::upload(device_, image, srgb));
            buildMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count();
            if (!srgb) images_[result] = std::move(image);   // ORM/normal data kept for per-part roughness lookups
            if (pendingSignature_ != 0) signatures_[pendingSignature_] = result;
        }
    }
    catch (const std::exception& failure)
    {
        CNA::Logger::Warn(std::string("living-room-simulator: could not rebuild mip chain: ") + failure.what());
        result = source;
    }
    if (pendingSignature_ != 0 && signatures_[pendingSignature_] == nullptr) signatures_[pendingSignature_] = result;
    pendingSignature_ = 0;
    remipped_[source] = result;
    return result;
}

const ImportedModel* ModelLibrary::load(const std::string& name, const std::string& path,
                                        const MaterialTweak& tweak)
{
    const auto cached = cache_.find(name);
    if (cached != cache_.end()) return cached->second.get();

    auto imported = std::make_unique<ImportedModel>();
    imported->name = name;
    ImportedModel* result = imported.get();
    cache_.emplace(name, std::move(imported));

    if (!std::filesystem::exists(path))
    {
        failures_.push_back(name + ": missing (" + path + ") -- run scripts/fetch-assets.sh and scripts/extract-assets.sh");
        return nullptr;
    }
    // A compiled .cnb (scripts/compile-assets.sh) next to the content root
    // loads in a fraction of the glTF import's time; it is used when it is
    // at least as new as the glTF it was compiled from.
    // The .cnb's textures are its siblings, resolved against its own
    // content manager's root (a .cnb loaded through the main manager looks
    // for them at the main root).
    bool compiled = false;
    if (compiled_ != nullptr && std::getenv("CNA_ROOM_NO_CNB") == nullptr)
    {
        std::error_code ec;
        const std::filesystem::path cnb = std::filesystem::path(compiled_->getRootDirectoryProperty()) / (name + ".cnb");
        compiled = std::filesystem::exists(cnb, ec)
                   && std::filesystem::last_write_time(cnb, ec) >= std::filesystem::last_write_time(path, ec);
    }
    const auto loadStart = std::chrono::steady_clock::now();
    try
    {
        result->model = std::make_unique<Model>(compiled ? compiled_->Load<Model>(name) : content_.Load<Model>(path));
        if (compiled) ++compiledLoads_;
    }
    catch (const std::exception& failure)
    {
        if (compiled)
        {
            // A stale or incompatible .cnb must not take the model down with it.
            CNA::Logger::Warn("living-room-simulator: model " + name + " -- .cnb load failed (" + failure.what() + "), importing the glTF");
            try
            {
                result->model = std::make_unique<Model>(content_.Load<Model>(path));
            }
            catch (const std::exception& again)
            {
                failures_.push_back(name + ": " + again.what());
                return nullptr;
            }
        }
        else
        {
            failures_.push_back(name + ": " + failure.what());
            return nullptr;
        }
    }

    const double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStart).count();
    const auto prepareStart = std::chrono::steady_clock::now();
    Vector3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    int index = 0;
    int repairedTangents = 0;
    std::vector<VertexBuffer*> repairedBuffers;
    const ModelMeshCollection& meshes = result->model->getMeshesProperty();
    for (int m = 0; m < meshes.getCountProperty(); ++m)
    {
        ModelMesh* mesh = meshes[m];
        if (mesh == nullptr) continue;
        const Matrix bone = absoluteTransform(*mesh);
        const ModelMeshPartCollection& parts = mesh->getMeshPartsProperty();
        for (int p = 0; p < parts.getCountProperty(); ++p)
        {
            ModelMeshPart* part = parts[p];
            if (part == nullptr || part->getPrimitiveCountProperty() <= 0) continue;
            if (VertexBuffer* buffer = part->getVertexBufferProperty();
                buffer != nullptr && std::find(repairedBuffers.begin(), repairedBuffers.end(), buffer) == repairedBuffers.end())
            {
                repairedBuffers.push_back(buffer);
                repairedTangents += repairTangents(*buffer);
            }

            Material material;
            material.name = name + "." + std::to_string(index);
            material.frontFaceCounterClockwise = true;
            if (const auto* pbr = dynamic_cast<const PbrEffect*>(part->getEffectProperty()))
            {
                material.baseColour = pbr->getDiffuseColorProperty();
                material.alpha = pbr->getAlphaProperty();
                material.metallic = pbr->getMetallicFactorProperty();
                material.roughness = pbr->getRoughnessFactorProperty();
                material.emissiveFactor = pbr->getEmissiveFactorProperty();
                material.albedo = remip(pbr->getTextureProperty(), true);
                material.normal = remip(pbr->getNormalMapProperty(), false);
                material.orm = remip(pbr->getMetallicRoughnessMapProperty(), false);
                Texture2D* occlusion = remip(pbr->getOcclusionMapProperty(), false);
                material.emissive = remip(pbr->getEmissiveMapProperty(), true);
                material.normalScale = pbr->getNormalScaleEXTProperty();
                material.occlusionStrength = pbr->getOcclusionStrengthEXTProperty();
                material.ior = pbr->getIorEXTProperty();
                material.specular = pbr->getSpecularFactorEXTProperty();
                material.alphaMode = pbr->getAlphaModeEXTProperty();
                material.alphaCutoff = pbr->getAlphaCutoffEXTProperty();
                material.doubleSided = pbr->getDoubleSidedEXTProperty();
                material.perSlotTransforms = true;
                material.slotTransforms = pbr->getTextureTransformsEXTProperty();
                material.slotTexCoords = pbr->getTextureCoordinateSetsEXTProperty();
                if (material.orm != nullptr)
                {
                    const auto image = images_.find(material.orm);
                    if (image != images_.end())
                    {
                        const auto& transform = material.slotTransforms[2];
                        material.roughnessMapMean = partRoughnessMean(*part, image->second, transform.Scale, transform.Offset);
                    }
                }
                // The occlusion slot is a separate texture in glTF; the room's
                // Material binds one ORM map to both slots, so keep a separate
                // pointer when they differ.
                material.occlusion = occlusion;
                if (material.alphaMode == AlphaModeEXT::Blend)
                {
                    // KHR_materials_transmission arrives as alpha = 1 - factor
                    // (CNA approximates it); fully transmissive glass would
                    // vanish, so keep a Fresnel-ish floor.
                    material.alpha = std::max(material.alpha, 0.30f);
                    material.castsShadow = false;
                    material.writesDepth = false;
                }
            }
            else
            {
                material.roughness = 0.85f;
            }
            if (std::getenv("CNA_ROOM_MODEL_DEBUG") != nullptr)
            {
                const auto describe = [](const Texture2D* t) {
                    return t == nullptr ? std::string("none")
                                        : std::to_string(t->getWidthProperty()) + "x" + std::to_string(t->getHeightProperty())
                                              + "/" + std::to_string(t->getLevelCountProperty());
                };
                CNA::Logger::Info("living-room-simulator:   part " + std::to_string(index) + " albedo " + describe(material.albedo)
                                  + " normal " + describe(material.normal) + " orm " + describe(material.orm)
                                  + " occl " + describe(material.occlusion) + " base " + fmt(material.baseColour.X) + ","
                                  + fmt(material.baseColour.Y) + "," + fmt(material.baseColour.Z) + " metal "
                                  + fmt(material.metallic) + " rough " + fmt(material.roughness) + " ior " + fmt(material.ior)
                                  + " spec " + fmt(material.specular) + " alpha "
                                  + fmt(material.alpha) + " mode " + std::to_string(static_cast<int>(material.alphaMode))
                                  + " uv sets " + std::to_string(material.slotTexCoords[0]) + std::to_string(material.slotTexCoords[1])
                                  + std::to_string(material.slotTexCoords[2]) + " scale " + fmt(material.slotTransforms[0].Scale.X)
                                  + "," + fmt(material.slotTransforms[0].Scale.Y) + " sided " + (material.doubleSided ? "2" : "1"));
            }
            if (tweak) tweak(material, index);
            const Material* stored = materials_.add(std::move(material));

            BoundingBox local;
            if (!partBounds(*part, local))
            {
                const BoundingSphere sphere = mesh->getBoundingSphereProperty();
                local = BoundingBox(sphere.Center - Vector3(sphere.Radius, sphere.Radius, sphere.Radius),
                                    sphere.Center + Vector3(sphere.Radius, sphere.Radius, sphere.Radius));
            }
            ImportedModel::Part entry;
            entry.mesh = std::make_unique<GpuMesh>(*part, local, name + "#" + std::to_string(index));
            entry.material = stored;
            entry.local = bone;
            entry.triangles = part->getPrimitiveCountProperty();
            result->triangles += entry.triangles;
            const BoundingBox world = transformBox(local, bone);
            lo = Vector3::Min(lo, world.Min);
            hi = Vector3::Max(hi, world.Max);
            result->parts.push_back(std::move(entry));
            ++index;
        }
    }
    result->bounds = result->parts.empty() ? BoundingBox(Vector3::Zero, Vector3::Zero) : BoundingBox(lo, hi);
    result->textureCount = remipped_.size();
    const Vector3 size = result->size();
    if (repairedTangents > 0)
        CNA::Logger::Info("living-room-simulator: model " + name + " -- repaired " + std::to_string(repairedTangents) + " degenerate tangents");
    const double prepareMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepareStart).count();
    CNA::Logger::Info("living-room-simulator: model " + name + " -- " + std::to_string(result->parts.size()) + " parts, "
                      + std::to_string(result->triangles) + " triangles, " + fmt(size.X) + " x " + fmt(size.Y)
                      + " x " + fmt(size.Z) + " m, min y " + fmt(lo.Y) + (compiled ? " [cnb" : " [gltf") + " load "
                      + std::to_string(loadMs) + " ms, prepare " + std::to_string(prepareMs) + " ms, "
                      + std::to_string(dedupedTextures_) + " textures shared, readback " + std::to_string(static_cast<int>(readbackMs_))
                      + " ms, mips " + std::to_string(static_cast<int>(buildMs_)) + " ms, signatures "
                      + std::to_string(static_cast<int>(signatureMs_)) + " ms so far]");
    return result;
}

Matrix ModelLibrary::placement(const ImportedModel& model, const Vector3& position, float yawRadians,
                               float fitHeight)
{
    const Vector3 size = model.size();
    float scale = 1.0f;
    if (fitHeight > 0.0f && size.Y > 1e-4f) scale = fitHeight / size.Y;
    const Vector3 footprint((model.bounds.Min.X + model.bounds.Max.X) * 0.5f, model.bounds.Min.Y,
                            (model.bounds.Min.Z + model.bounds.Max.Z) * 0.5f);
    return Matrix::CreateTranslation(-footprint) * Matrix::CreateScale(scale) * Matrix::CreateRotationY(yawRadians)
           * Matrix::CreateTranslation(position);
}

void ModelLibrary::place(SceneRenderer& renderer, const ImportedModel& model, const Matrix& world,
                         const std::string& name, bool castsShadow) const
{
    for (const ImportedModel::Part& part : model.parts)
    {
        SceneItem item;
        item.mesh = part.mesh.get();
        item.material = part.material;
        item.world = part.local * world;
        item.castsShadow = castsShadow && part.material->castsShadow;
        item.name = name;
        renderer.addItem(std::move(item));
    }
}

}  // namespace CnaRoom
