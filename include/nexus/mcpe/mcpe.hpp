#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexus::mcpe {

// Minecraft PE 0.14.3 uses a custom material/shader pipeline:
//   assets/shaders/uniforms.json  declares named uniform groups and types
//   assets/materials/**/*.material declare passes that reference shaders
//   shaders use `// __multiversion__` and are compiled as GLSL ES 1.00 or
//   3.00 depending on the device.
//
// This module reads that real metadata.  It does not contain any proprietary
// asset; callers point it at an extracted APK that they own.

struct UniformDefinition {
    std::string name;
    std::string type;      // Float1, Float2, Float4, Matrix4x4, ...
    std::string glsl_type; // float, vec2, vec4, mat4, ...
    int count = 1;         // scalar/vector component count
    int matrix_dim = 0;    // 4 for Matrix4x4, otherwise 0
};

struct UniformGroup {
    std::string name;
    std::vector<UniformDefinition> uniforms;
};

struct UniformCatalog {
    std::vector<UniformGroup> groups;
    bool loaded = false;
    std::string source_path;

    bool load(const std::string& path, std::string& error);
    const UniformDefinition* find(const std::string& name) const;
    std::vector<std::string> names() const;
    nlohmann::json to_json() const;
};

const char* glsl_type_for_uniform(const std::string& type);

struct MaterialPass {
    std::string name;
    std::string parent;       // from the "child:parent" material name convention
    std::vector<std::string> states;
    std::vector<std::string> defines;
    std::string blend_src;
    std::string blend_dst;
    std::string vertex_shader;
    std::string fragment_shader;
    std::string geometry_shader;
    bool resolved = false;    // inheritance resolved by load_material_set()
    nlohmann::json raw = nlohmann::json::object();
};

struct MaterialFile {
    std::string path;
    std::vector<MaterialPass> passes;
    std::vector<std::string> includes;
    bool ok = false;
    std::string error;
};

// Parses one *material JSON file.  Include-list files (arrays of paths) are
// reported through MaterialFile::includes instead of being treated as passes.
bool parse_material_file(const std::string& path, MaterialFile& material, std::string& error);
bool parse_material_json(const nlohmann::json& json, const std::string& source_path,
                         MaterialFile& material, std::string& error);

struct MaterialSet {
    std::vector<MaterialFile> files;
    std::vector<std::string> referenced_shaders;
    std::vector<std::string> issues;
    size_t pass_count = 0;
    nlohmann::json to_json() const;
};

// Loads every *.material / *.json file below `materials_root`.  Missing files
// are reported, not invented.
MaterialSet load_material_set(const std::string& materials_root);

struct ShaderAsset {
    std::string path;       // e.g. shaders/terrain.vertex
    std::string stage;      // vertex, fragment, geometry
    bool referenced = false;
    bool exists = false;
};

struct AssetSummary {
    std::string assets_root;
    UniformCatalog uniforms;
    MaterialSet materials;
    std::vector<ShaderAsset> shaders;
    std::vector<std::string> missing_shaders;
    std::vector<std::string> issues;
    nlohmann::json to_json() const;
    std::string to_markdown() const;
};

// Inspects an extracted assets directory (or an APK install's assets/ dir).
AssetSummary inspect_assets(const std::string& assets_root);

// Strips the `// __multiversion__` marker and prepares a concrete shader
// source for the requested GLSL ES version.  The MCPE loader normally
// prepends the #version line; NEXUS performs the equivalent step explicitly
// and defines __VERSION__ so the `#if __VERSION__ >= 300` branches select the
// same code the game would compile.
std::string prepare_multiversion_source(const std::string& source, int glsl_version);

// True when the source begins with the MCPE multiversion marker.
bool has_multiversion_marker(const std::string& source);

// Maps a material shader path to the extracted asset path, rejecting paths
// that escape the asset root.
std::string resolve_shader_path(const std::string& assets_root, const std::string& shader_path);

} // namespace nexus::mcpe
