// SPDX-License-Identifier: MIT
#include "CnaRoom/Effects/FiniteHdrPass.hpp"
#include "CnaRoom/Effects/ShaderDiagnostics.hpp"

#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include <string>
#include <vector>

namespace CnaRoom::Effects {
namespace {

// This guard addresses the observed EasyGL PBR output recorded in CNA_FINDINGS.md.
// Other renderer languages keep their existing path when this package is unavailable.
const char* vertexBody = R"GLSL(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
uniform mat4 projection;
out vec2 TexCoord;
out vec4 SpriteColor;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    SpriteColor = aColor;
}
)GLSL";

const char* fragmentBody = R"GLSL(
in vec2 TexCoord;
in vec4 SpriteColor;
uniform sampler2D texture1;
out vec4 FragColor;
bool finiteColour(vec4 c) { return !any(isnan(c)) && !any(isinf(c)); }
void main() {
    ivec2 extent = textureSize(texture1, 0);
    ivec2 at = clamp(ivec2(TexCoord * vec2(extent)), ivec2(0), extent - 1);
    vec4 colour = texelFetch(texture1, at, 0);
    if (!finiteColour(colour)) {
        vec4 sum = vec4(0.0);
        float count = 0.0;
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                vec4 candidate = texelFetch(texture1, clamp(at + ivec2(x, y), ivec2(0), extent - 1), 0);
                if (finiteColour(candidate)) { sum += candidate; count += 1.0; }
            }
        }
        colour = count > 0.0 ? sum / count : vec4(0.0, 0.0, 0.0, 1.0);
    }
    FragColor = colour * SpriteColor;
}
)GLSL";

CNA::Graphics::ShaderPackageEXT package()
{
    using CNA::Graphics::ShaderCodeEXT;
    using CNA::ShaderLanguageEXT;
    using CNA::ShaderStageEXT;
    const std::string es = "#version 300 es\nprecision highp float;\nprecision highp sampler2D;\n";
    const std::string desktop = "#version 330 core\n";
    return CNA::Graphics::ShaderPackageEXT(std::vector<ShaderCodeEXT>{
        {ShaderLanguageEXT::GlslEs, ShaderStageEXT::Vertex, "main", "finite_hdr.es.vert", es + vertexBody},
        {ShaderLanguageEXT::GlslEs, ShaderStageEXT::Fragment, "main", "finite_hdr.es.frag", es + fragmentBody},
        {ShaderLanguageEXT::GlslDesktop, ShaderStageEXT::Vertex, "main", "finite_hdr.desktop.vert", desktop + vertexBody},
        {ShaderLanguageEXT::GlslDesktop, ShaderStageEXT::Fragment, "main", "finite_hdr.desktop.frag", desktop + fragmentBody}
    }, {ShaderStageEXT::Vertex, ShaderStageEXT::Fragment}, {
        {"texture1", 0, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D, ShaderStageEXT::Fragment}
    });
}

} // namespace

FiniteHdrPass::FiniteHdrPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    : fullscreen_(device)
{
    const auto shaders = package();
    if (shaders.selectFor(device).isUsable())
        effect_ = std::make_unique<Microsoft::Xna::Framework::Graphics::ShaderEffect>(device, shaders);
    bool logged = false;
    detail::reportShaderCompileFailure(device, "FiniteHdrPass", effect_.get(), logged);
}

FiniteHdrPass::~FiniteHdrPass() = default;

void FiniteHdrPass::apply(const PostProcessContext& context)
{
    fullscreen_.draw(context.source, context.destination, effect_.get(), context.width, context.height);
}

const std::string& FiniteHdrPass::getName() const
{
    static const std::string name = "FiniteHDR";
    return name;
}

bool FiniteHdrPass::isSupported(Microsoft::Xna::Framework::Graphics::GraphicsDevice&) const
{
    return effect_ && effect_->IsEffectValid();
}

} // namespace CnaRoom::Effects
