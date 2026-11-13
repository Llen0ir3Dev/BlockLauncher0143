#include <nexus/mcpe/mcpe.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include <nexus/core/fs.hpp>
#include <nexus/core/strings.hpp>

namespace nexus::mcpe {

namespace {

std::string json_string(const nlohmann::json& json, const char* key,
                        const std::string& fallback = "") {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

std::string extension_stage(const std::string& path) {
    const std::string ext = to_lower(extension(path));
    if (ext == ".vertex" || ext == ".vert" || ext == ".vsh") return "vertex";
    if (ext == ".fragment" || ext == ".frag" || ext == ".fsh") return "fragment";
    if (ext == ".geometry" || ext == ".geom" || ext == ".gsh") return "geometry";
    return "";
}

// The shipped MCPE *.material files contain // and /* */ comments, which are
// not part of strict JSON.  Remove them while preserving string literals and
// line structure so parse errors still point at the right line.
std::string strip_json_comments(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool in_string = false;
    bool escaped = false;
    bool line_comment = false;
    bool block_comment = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (line_comment) {
            if (c == '\n') {
                line_comment = false;
                out.push_back(c);
            } else {
                out.push_back(' ');
            }
            continue;
        }
        if (block_comment) {
            if (c == '*' && next == '/') {
                out.append("  ");
                block_comment = false;
                ++i;
            } else {
                out.push_back(c == '\n' ? '\n' : ' ');
            }
            continue;
        }
        if (in_string) {
            out.push_back(c);
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') {
            in_string = true;
            out.push_back(c);
            continue;
        }
        if (c == '/' && next == '/') {
            line_comment = true;
            out.append("  ");
            ++i;
            continue;
        }
        if (c == '/' && next == '*') {
            block_comment = true;
            out.append("  ");
            ++i;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> json_string_array(const nlohmann::json& json, const char* key) {
    std::vector<std::string> out;
    const auto it = json.find(key);
    if (it == json.end() || !it->is_array()) return out;
    for (const auto& item : *it) {
        if (item.is_string()) out.push_back(item.get<std::string>());
    }
    return out;
}

} // namespace

const char* glsl_type_for_uniform(const std::string& type) {
    if (type == "Float1" || type == "Int1" || type == "Bool1") return "float";
    if (type == "Float2" || type == "Int2" || type == "Bool2") return "vec2";
    if (type == "Float3" || type == "Int3" || type == "Bool3") return "vec3";
    if (type == "Float4" || type == "Int4" || type == "Bool4") return "vec4";
    if (type == "Matrix4x4") return "mat4";
    if (type == "Matrix3x3") return "mat3";
    if (type == "Matrix2x2") return "mat2";
    return "";
}

bool UniformCatalog::load(const std::string& path, std::string& error) {
    groups.clear();
    loaded = false;
    source_path = path;
    std::string text;
    if (!fs::read_file(path, text, 4ull * 1024 * 1024)) {
        error = "cannot read uniform catalog: " + path;
        return false;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(text);
    } catch (const std::exception& ex) {
        error = "invalid JSON in " + path + ": " + ex.what();
        return false;
    }
    if (!json.is_object()) {
        error = "uniform catalog must be a JSON object";
        return false;
    }
    for (auto group_it = json.begin(); group_it != json.end(); ++group_it) {
        if (!group_it.value().is_array()) {
            error = "uniform group '" + group_it.key() + "' is not an array";
            return false;
        }
        UniformGroup group;
        group.name = group_it.key();
        for (const auto& item : group_it.value()) {
            if (!item.is_object()) continue;
            UniformDefinition definition;
            definition.name = json_string(item, "Name");
            definition.type = json_string(item, "Type");
            definition.glsl_type = glsl_type_for_uniform(definition.type);
            if (definition.type.rfind("Matrix4", 0) == 0) definition.matrix_dim = 4;
            if (definition.type.rfind("Float", 0) == 0) {
                definition.count = std::max(1, definition.type.back() - '0');
            } else if (definition.type.rfind("Int", 0) == 0 ||
                       definition.type.rfind("Bool", 0) == 0) {
                definition.count = std::max(1, definition.type.back() - '0');
            }
            if (definition.name.empty()) continue;
            if (definition.glsl_type.empty()) {
                error = "unsupported uniform type '" + definition.type + "' for " +
                        definition.name;
                return false;
            }
            group.uniforms.push_back(std::move(definition));
        }
        groups.push_back(std::move(group));
    }
    loaded = true;
    return true;
}

const UniformDefinition* UniformCatalog::find(const std::string& name) const {
    for (const auto& group : groups) {
        for (const auto& uniform : group.uniforms) {
            if (uniform.name == name) return &uniform;
        }
    }
    return nullptr;
}

std::vector<std::string> UniformCatalog::names() const {
    std::vector<std::string> out;
    for (const auto& group : groups) {
        for (const auto& uniform : group.uniforms) out.push_back(uniform.name);
    }
    return out;
}

nlohmann::json UniformCatalog::to_json() const {
    nlohmann::json groups_json = nlohmann::json::array();
    for (const auto& group : groups) {
        nlohmann::json uniforms = nlohmann::json::array();
        for (const auto& uniform : group.uniforms) {
            uniforms.push_back({{"name", uniform.name}, {"mcpeType", uniform.type},
                                {"glslType", uniform.glsl_type}, {"count", uniform.count},
                                {"matrixDim", uniform.matrix_dim}});
        }
        groups_json.push_back({{"name", group.name}, {"uniforms", uniforms}});
    }
    return {{"loaded", loaded}, {"source", source_path}, {"groups", groups_json}};
}

bool parse_material_json(const nlohmann::json& json, const std::string& source_path,
                         MaterialFile& material, std::string& error) {
    material = MaterialFile{};
    material.path = source_path;
    material.ok = false;
    if (json.is_array()) {
        for (const auto& item : json) {
            if (item.is_object() && item.contains("path") && item["path"].is_string()) {
                material.includes.push_back(item["path"].get<std::string>());
            } else if (item.is_string()) {
                material.includes.push_back(item.get<std::string>());
            }
        }
        material.ok = true;
        return true;
    }
    if (!json.is_object()) {
        error = "material file must contain a JSON object";
        return false;
    }
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (!it.value().is_object()) {
            error = "material '" + it.key() + "' is not an object";
            return false;
        }
        MaterialPass pass;
        pass.name = it.key();
        pass.raw = it.value();
        const auto colon = pass.name.find(':');
        if (colon != std::string::npos && colon + 1 < pass.name.size()) {
            pass.parent = pass.name.substr(colon + 1);
        }
        const auto& value = it.value();
        if (value.contains("states") && value["states"].is_array()) {
            for (const auto& state : value["states"]) {
                if (state.is_string()) pass.states.push_back(state.get<std::string>());
            }
        }
        pass.defines = json_string_array(value, "defines");
        pass.blend_src = json_string(value, "blendSrc");
        pass.blend_dst = json_string(value, "blendDst");
        pass.vertex_shader = json_string(value, "vertexShader");
        pass.fragment_shader = json_string(value, "fragmentShader");
        pass.geometry_shader = json_string(value, "geometryShader");
        // A pass may inherit shaders from its parent (child:parent), so the
        // absence of shader fields is not an error here; load_material_set()
        // resolves inheritance and validates the final passes.
        material.passes.push_back(std::move(pass));
    }
    material.ok = true;
    return true;
}

bool parse_material_file(const std::string& path, MaterialFile& material, std::string& error) {
    std::string text;
    if (!fs::read_file(path, text, 4ull * 1024 * 1024)) {
        error = "cannot read material file: " + path;
        return false;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(strip_json_comments(text));
    } catch (const std::exception& ex) {
        error = "invalid JSON in " + path + ": " + ex.what();
        return false;
    }
    return parse_material_json(json, path, material, error);
}

nlohmann::json MaterialSet::to_json() const {
    nlohmann::json files_json = nlohmann::json::array();
    for (const auto& file : files) {
        nlohmann::json passes = nlohmann::json::array();
        for (const auto& pass : file.passes) {
            passes.push_back({{"name", pass.name}, {"parent", pass.parent},
                              {"states", pass.states}, {"defines", pass.defines},
                              {"blendSrc", pass.blend_src}, {"blendDst", pass.blend_dst},
                              {"vertexShader", pass.vertex_shader},
                              {"fragmentShader", pass.fragment_shader},
                              {"geometryShader", pass.geometry_shader},
                              {"resolved", pass.resolved}});
        }
        files_json.push_back({{"path", file.path}, {"ok", file.ok}, {"error", file.error},
                              {"includes", file.includes}, {"passes", passes}});
    }
    return {{"passCount", pass_count}, {"files", files_json},
            {"referencedShaders", referenced_shaders}, {"issues", issues}};
}

MaterialSet load_material_set(const std::string& materials_root) {
    MaterialSet set;
    if (!fs::is_directory(materials_root)) {
        set.issues.push_back("materials directory does not exist: " + materials_root);
        return set;
    }
    for (const auto& path : fs::list_directory(materials_root, true, ".material")) {
        MaterialFile material;
        std::string error;
        if (!parse_material_file(path, material, error)) {
            material.ok = false;
            material.error = error;
            set.issues.push_back(error);
        }
        set.files.push_back(std::move(material));
    }
    // Some material files are include lists that reference more .material
    // files; surface them but do not recurse outside the supplied root.
    for (const auto& path : fs::list_directory(materials_root, true, ".json")) {
        MaterialFile material;
        std::string error;
        if (!parse_material_file(path, material, error)) continue;
        if (!material.includes.empty()) {
            for (const auto& include : material.includes) {
                set.issues.push_back("material include reference: " + include + " (in " +
                                     path + ")");
            }
        }
        set.files.push_back(std::move(material));
    }

    // MCPE materials inherit through the "child:parent" name convention, and
    // parents can live in another .material file.  Resolve every chain across
    // the whole set before validating or counting passes.
    std::map<std::string, std::pair<size_t, size_t>> index;
    std::set<std::string> parent_names;
    for (size_t file_index = 0; file_index < set.files.size(); ++file_index) {
        auto& file = set.files[file_index];
        for (size_t pass_index = 0; pass_index < file.passes.size(); ++pass_index) {
            const auto& pass = file.passes[pass_index];
            if (index.find(pass.name) == index.end()) {
                index[pass.name] = {file_index, pass_index};
            }
            if (!pass.parent.empty()) parent_names.insert(pass.parent);
        }
    }

    std::map<std::string, MaterialPass> resolved;
    std::set<std::string> visiting;
    std::set<std::string> reported_parents;
    std::function<MaterialPass(const std::string&)> resolve =
        [&](const std::string& name) -> MaterialPass {
        const auto memo_it = resolved.find(name);
        if (memo_it != resolved.end()) return memo_it->second;
        const auto index_it = index.find(name);
        if (index_it == index.end()) return MaterialPass{};
        MaterialPass pass = set.files[index_it->second.first].passes[index_it->second.second];
        pass.name = name;
        if (!pass.parent.empty()) {
            if (index.find(pass.parent) == index.end()) {
                if (reported_parents.insert(pass.parent).second) {
                    set.issues.push_back("material parent not found: " + pass.parent +
                                         " (referenced by " + name + ")");
                }
            } else if (visiting.count(pass.parent) > 0) {
                set.issues.push_back("material inheritance cycle at " + name);
            } else {
                visiting.insert(name);
                const MaterialPass parent = resolve(pass.parent);
                visiting.erase(name);
                if (pass.blend_src.empty()) pass.blend_src = parent.blend_src;
                if (pass.blend_dst.empty()) pass.blend_dst = parent.blend_dst;
                if (pass.vertex_shader.empty()) pass.vertex_shader = parent.vertex_shader;
                if (pass.fragment_shader.empty()) pass.fragment_shader = parent.fragment_shader;
                if (pass.geometry_shader.empty()) pass.geometry_shader = parent.geometry_shader;
                std::vector<std::string> states = parent.states;
                for (const auto& state : pass.states) {
                    if (std::find(states.begin(), states.end(), state) == states.end()) {
                        states.push_back(state);
                    }
                }
                pass.states = std::move(states);
                std::vector<std::string> defines = parent.defines;
                for (const auto& define : pass.defines) {
                    if (std::find(defines.begin(), defines.end(), define) == defines.end()) {
                        defines.push_back(define);
                    }
                }
                pass.defines = std::move(defines);
            }
        }
        pass.resolved = true;
        resolved[name] = pass;
        return pass;
    };

    std::set<std::string> shaders;
    set.pass_count = 0;
    for (auto& file : set.files) {
        for (auto& pass : file.passes) {
            pass = resolve(pass.name);
            // Abstract parents are not expected to have shaders of their own.
            if (pass.vertex_shader.empty() && pass.fragment_shader.empty() &&
                parent_names.count(pass.name) == 0) {
                set.issues.push_back("material '" + pass.name +
                                     "' references no vertex or fragment shader");
            }
            if (!pass.vertex_shader.empty()) shaders.insert(pass.vertex_shader);
            if (!pass.fragment_shader.empty()) shaders.insert(pass.fragment_shader);
            if (!pass.geometry_shader.empty()) shaders.insert(pass.geometry_shader);
            ++set.pass_count;
        }
    }
    set.referenced_shaders.assign(shaders.begin(), shaders.end());
    return set;
}

std::string resolve_shader_path(const std::string& assets_root, const std::string& shader_path) {
    if (shader_path.empty()) return "";
    if (!fs::is_safe_relative_path(shader_path)) return "";
    return fs::join(assets_root, shader_path);
}

bool has_multiversion_marker(const std::string& source) {
    return source.find("__multiversion__") != std::string::npos;
}

std::string prepare_multiversion_source(const std::string& source, int glsl_version) {
    std::istringstream in(source);
    std::ostringstream out;
    std::string line;
    bool had_version = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = trim(line);
        if (trimmed == "// __multiversion__") continue;
        if (starts_with(trimmed, "#version")) {
            had_version = true;
            out << (glsl_version >= 300 ? "#version 300 es" : "#version 100") << "\n";
            continue;
        }
        out << line << "\n";
    }
    std::ostringstream result;
    if (!had_version) {
        result << (glsl_version >= 300 ? "#version 300 es" : "#version 100") << "\n";
    }
    result << "#ifndef __VERSION__\n#define __VERSION__ " << glsl_version
           << "\n#endif\n";
    // MCPE's shader loader injects its own shorthand types before compiling.
    // Reproduce them so `uniform MAT4 ...` and `POS4` declarations parse the
    // same way they do in the game.
    result << "#ifndef MAT4\n"
              "#define MAT4 mat4\n"
              "#define MAT3 mat3\n"
              "#define MAT2 mat2\n"
              "#define POS4 vec4\n"
              "#define POS3 vec3\n"
              "#define POS2 vec2\n"
              "#endif\n";
    result << out.str();
    return result.str();
}

AssetSummary inspect_assets(const std::string& assets_root) {
    AssetSummary summary;
    summary.assets_root = assets_root;
    const std::string uniforms_path = fs::join(assets_root, "shaders/uniforms.json");
    std::string error;
    if (!summary.uniforms.load(uniforms_path, error)) {
        summary.issues.push_back(error);
    }
    summary.materials = load_material_set(fs::join(assets_root, "materials"));
    for (const auto& issue : summary.materials.issues) summary.issues.push_back(issue);

    std::set<std::string> referenced(summary.materials.referenced_shaders.begin(),
                                     summary.materials.referenced_shaders.end());
    const std::string shaders_root = fs::join(assets_root, "shaders");
    if (fs::is_directory(shaders_root)) {
        for (const auto& path : fs::list_directory(shaders_root, true)) {
            const std::string relative = path.substr(assets_root.size() +
                                                     (assets_root.back() == '/' ? 0 : 1));
            const std::string stage = extension_stage(relative);
            if (stage.empty()) continue;
            ShaderAsset asset;
            asset.path = relative;
            asset.stage = stage;
            asset.referenced = referenced.count(relative) > 0;
            asset.exists = true;
            summary.shaders.push_back(std::move(asset));
        }
    }
    for (const auto& reference : summary.materials.referenced_shaders) {
        const std::string path = resolve_shader_path(assets_root, reference);
        if (path.empty()) {
            summary.issues.push_back("unsafe material shader path: " + reference);
            continue;
        }
        bool found = false;
        for (const auto& asset : summary.shaders) {
            if (asset.path == reference) { found = true; break; }
        }
        if (!found) {
            // 0.14.3 targets GLES 2.0, which has no geometry shader stage.
            // Materials still list geometry shaders for the newer pipeline;
            // report that fact without treating it as a broken pack.
            if (extension_stage(reference) == "geometry") {
                summary.issues.push_back(
                    "geometry shader referenced but unavailable on GLES 2.0: " + reference);
            } else {
                summary.missing_shaders.push_back(reference);
            }
        }
    }
    std::sort(summary.shaders.begin(), summary.shaders.end(),
              [](const ShaderAsset& a, const ShaderAsset& b) { return a.path < b.path; });
    return summary;
}

nlohmann::json AssetSummary::to_json() const {
    nlohmann::json shader_json = nlohmann::json::array();
    for (const auto& shader : shaders) {
        shader_json.push_back({{"path", shader.path}, {"stage", shader.stage},
                               {"referenced", shader.referenced}, {"exists", shader.exists}});
    }
    return {{"assetsRoot", assets_root}, {"uniforms", uniforms.to_json()},
            {"materials", materials.to_json()}, {"shaders", shader_json},
            {"missingShaders", missing_shaders}, {"issues", issues}};
}

std::string AssetSummary::to_markdown() const {
    std::ostringstream out;
    out << "# Minecraft PE asset summary\n\n";
    out << "- Assets root: `" << assets_root << "`\n";
    out << "- Uniform groups: " << uniforms.groups.size() << "\n";
    out << "- Material files: " << materials.files.size() << "\n";
    out << "- Material passes: " << materials.pass_count << "\n";
    out << "- Shader files: " << shaders.size() << "\n";
    out << "- Referenced shaders: " << materials.referenced_shaders.size() << "\n";
    out << "- Missing referenced shaders: " << missing_shaders.size() << "\n\n";
    if (!uniforms.groups.empty()) {
        out << "## Uniform groups\n\n";
        for (const auto& group : uniforms.groups) {
            out << "- **" << group.name << "**: ";
            for (size_t i = 0; i < group.uniforms.size(); ++i) {
                if (i > 0) out << ", ";
                out << "`" << group.uniforms[i].name << "` (" << group.uniforms[i].glsl_type << ")";
            }
            out << "\n";
        }
        out << "\n";
    }
    if (!missing_shaders.empty()) {
        out << "## Missing shaders\n\n";
        for (const auto& shader : missing_shaders) out << "- `" << shader << "`\n";
        out << "\n";
    }
    if (!issues.empty()) {
        out << "## Issues\n\n";
        for (const auto& issue : issues) out << "- " << issue << "\n";
    }
    return out.str();
}

} // namespace nexus::mcpe
