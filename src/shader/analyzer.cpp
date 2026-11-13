#include <nexus/shader/analyzer.hpp>

#include <algorithm>
#include <set>
#include <sstream>
#include <unordered_set>

#include <nexus/core/strings.hpp>

namespace nexus::shader {

const char* severity_name(IssueSeverity severity) {
    switch (severity) {
        case IssueSeverity::Info: return "info";
        case IssueSeverity::Warning: return "warning";
        case IssueSeverity::Error: return "error";
    }
    return "info";
}

namespace {

const std::unordered_set<std::string>& optifine_names() {
    static const std::unordered_set<std::string> names = {
        "gbufferModelView", "gbufferModelViewInverse", "gbufferProjection",
        "gbufferProjectionInverse", "gbufferPreviousModelView", "gbufferPreviousProjection",
        "shadowModelView", "shadowModelViewInverse", "shadowProjection",
        "shadowProjectionInverse", "sunPosition", "moonPosition", "shadowLightPosition",
        "cameraPosition", "previousCameraPosition", "frameTime", "frameCounter",
        "worldTime", "worldDay", "rainStrength", "wetness", "viewWidth", "viewHeight",
        "near", "far", "aspectRatio", "centerDepthSmooth", "isEyeInWater", "nightVision",
        "blindness", "screenBrightness", "hideGUI", "fogColor", "skyColor", "sunColor",
        "moonColor", "upColor", "eyeBrightness", "eyeBrightnessSmooth", "heldItemId",
        "heldBlockLightValue", "heldItemId2", "heldBlockLightValue2", "entityId",
        "entityColor", "blockEntityId", "atlasSize", "textureAtlasSize", "alphaTestRef"
    };
    return names;
}

const std::unordered_set<std::string>& unsupported_builtins() {
    static const std::unordered_set<std::string> names = {
        "texelFetch", "texelFetchOffset", "textureGather", "textureGatherOffset",
        "imageLoad", "imageStore", "imageAtomicAdd", "atomicAdd", "atomicMin",
        "atomicMax", "atomicAnd", "atomicOr", "atomicXor", "atomicExchange",
        "atomicCompSwap", "textureGrad", "textureGradOffset", "textureProjGrad",
        "textureLodOffset", "textureProjLod", "textureProjLodOffset",
        "textureOffset", "textureProjOffset", "textureProj", "textureSize",
        "textureQueryLevels", "textureSamples", "textureQueryLod",
        "inverse", "determinant", "transpose", "outerProduct", "matrixCompMult",
        "packHalf2x16", "unpackHalf2x16", "packUnorm2x16", "unpackUnorm2x16",
        "bitfieldExtract", "bitfieldInsert", "bitfieldReverse", "findLSB", "findMSB",
        "floatBitsToInt", "intBitsToFloat", "uintBitsToFloat", "floatBitsToUint",
        "isnan", "isinf", "roundEven", "trunc", "round"
    };
    return names;
}

void add_issue(AnalysisResult& result, IssueSeverity severity, const std::string& code,
               const std::string& message, const SourceLoc& loc = {}, const std::string& hint = "") {
    Issue issue;
    issue.severity = severity;
    issue.code = code;
    issue.message = message;
    issue.loc = loc;
    issue.hint = hint;
    result.issues.push_back(std::move(issue));
}

int array_size_from_dims(const TypeSpec& type) {
    if (type.array_dims.empty() || type.array_dims[0].empty()) return 0;
    try {
        return std::stoi(type.array_dims[0]);
    } catch (...) {
        return -1;
    }
}

ShaderVariable make_variable(const VarDecl& decl, const std::string& qualifier) {
    ShaderVariable var;
    var.name = decl.name;
    var.type = decl.type.base;
    var.qualifier = qualifier;
    var.array_size = array_size_from_dims(decl.type);
    var.loc = decl.loc;
    return var;
}

std::string primary_qualifier(const TypeSpec& type) {
    static const char* order[] = {"uniform", "attribute", "varying", "in", "out", "inout"};
    for (const char* q : order) {
        if (type.has_qualifier(q)) return q;
    }
    if (type.has_qualifier("const")) return "const";
    return "";
}

void visit_expr(const Expr& expr, AnalysisResult& result, ShaderInfo& info) {
    switch (expr.kind) {
        case Expr::Kind::Identifier: {
            if (expr.value == "gl_FragColor") info.uses_frag_color = true;
            if (expr.value == "gl_FragData") info.uses_frag_data = true;
            if (expr.value.rfind("gl_", 0) == 0) {
                static const std::unordered_set<std::string> supported = {
                    "gl_Position", "gl_PointSize", "gl_FragCoord", "gl_FrontFacing",
                    "gl_PointCoord", "gl_FragColor", "gl_FragData", "gl_DepthRange",
                    "gl_MaxVertexAttribs", "gl_MaxVertexUniformVectors",
                    "gl_MaxVaryingVectors", "gl_MaxFragmentUniformVectors",
                    "gl_MaxTextureImageUnits", "gl_MaxCombinedTextureImageUnits",
                    "gl_MaxDrawBuffers", "gl_MaxVertexTextureImageUnits"
                };
                if (!supported.count(expr.value)) info.unsupported_builtins.push_back(expr.value);
            }
            if (optifine_names().count(expr.value)) info.optifine_uniforms.push_back(expr.value);
            break;
        }
        case Expr::Kind::Call: {
            const std::string& name = expr.value;
            if (name == "textureLod" || name == "textureLodOffset") info.uses_texture_lod = true;
            if (name == "texelFetch" || name == "texelFetchOffset") info.uses_texel_fetch = true;
            if (name == "dFdx" || name == "dFdy" || name == "fwidth") info.uses_derivatives = true;
            if (name == "discard") info.uses_discard = true;
            if (unsupported_builtins().count(name)) info.unsupported_builtins.push_back(name);
            break;
        }
        default:
            break;
    }
    for (const auto& arg : expr.args) {
        if (arg) visit_expr(*arg, result, info);
    }
}

void visit_statement(const Stmt& stmt, AnalysisResult& result, ShaderInfo& info) {
    if (stmt.kind == Stmt::Kind::Switch) info.uses_switch = true;
    if (stmt.kind == Stmt::Kind::Discard) info.uses_discard = true;
    if (stmt.expr) visit_expr(*stmt.expr, result, info);
    if (stmt.cond) visit_expr(*stmt.cond, result, info);
    if (stmt.init_expr) visit_expr(*stmt.init_expr, result, info);
    if (stmt.incr) visit_expr(*stmt.incr, result, info);
    for (const auto& decl : stmt.decls) {
        if (decl && decl->init) visit_expr(*decl->init, result, info);
    }
    for (const auto& child : stmt.body) {
        if (child) visit_statement(*child, result, info);
    }
    if (stmt.else_branch) visit_statement(*stmt.else_branch, result, info);
}

} // namespace

AnalysisResult analyze_shader(const TranslationUnit& unit, ShaderStage stage) {
    AnalysisResult result;
    ShaderInfo& info = result.info;
    info.stage = stage;
    info.version = unit.version;
    info.es = unit.es;
    info.function_count = static_cast<int>(unit.functions.size());
    info.global_count = static_cast<int>(unit.globals.size());

    if (unit.version >= 300 && !unit.es) {
        add_issue(result, IssueSeverity::Warning, "desktop_glsl_version",
                  "Desktop GLSL " + std::to_string(unit.version) +
                      " is not directly supported by the MCPE GLES2 pipeline; translation will adapt common constructs.",
                  {}, "Target profile gles100 is used by default.");
    }
    if (unit.version >= 400) {
        add_issue(result, IssueSeverity::Error, "glsl_version_too_new",
                  "GLSL " + std::to_string(unit.version) +
                      " introduces features that cannot be represented in GLSL ES 1.00.");
    }
    if (unit.version == 300 && unit.es) {
        add_issue(result, IssueSeverity::Warning, "gles300_source",
                  "GLSL ES 3.00 source requires several rewrites for a GLES2 target.",
                  {}, "in/out and texture() are translated; integer varyings may be unsupported.");
    }

    for (const auto& global : unit.globals) {
        const std::string qualifier = primary_qualifier(global.type);
        const std::string base = global.type.base;
        if (base == "uint" || base == "uvec2" || base == "uvec3" || base == "uvec4") info.uses_uint = true;
        if (base == "double" || base.rfind("dvec", 0) == 0) info.uses_double = true;
        if (global.type.has_qualifier("uniform")) {
            info.uniforms.push_back(make_variable(global, "uniform"));
            if (base.rfind("sampler", 0) == 0 || base.rfind("isampler", 0) == 0 ||
                base.rfind("usampler", 0) == 0) {
                info.samplers.push_back(global.name);
            }
            if (optifine_names().count(global.name)) info.optifine_uniforms.push_back(global.name);
        } else if (global.type.has_qualifier("attribute") ||
                   (stage == ShaderStage::Vertex && global.type.has_qualifier("in"))) {
            info.attributes.push_back(make_variable(global, "attribute"));
        } else if (global.type.has_qualifier("varying") ||
                   (stage == ShaderStage::Vertex && global.type.has_qualifier("out")) ||
                   (stage == ShaderStage::Fragment && global.type.has_qualifier("in"))) {
            info.varyings.push_back(make_variable(global, "varying"));
        } else if (stage == ShaderStage::Fragment && global.type.has_qualifier("out")) {
            info.outputs.push_back(make_variable(global, "out"));
        }
        if (global.init) visit_expr(*global.init, result, info);
    }

    for (const auto& fn : unit.functions) {
        info.functions.push_back(fn.name);
        if (fn.name == "main") info.has_main = true;
        if (unit.version >= 150 && (fn.name == "main" || !fn.prototype)) {
            // no-op; function bodies are inspected below
        }
        if (fn.body) visit_statement(*fn.body, result, info);
    }

    if (!info.has_main) {
        add_issue(result, IssueSeverity::Error, "missing_main", "No main() function was found.");
    }
    if (info.unsupported_builtins.size()) {
        std::sort(info.unsupported_builtins.begin(), info.unsupported_builtins.end());
        info.unsupported_builtins.erase(std::unique(info.unsupported_builtins.begin(), info.unsupported_builtins.end()),
                                        info.unsupported_builtins.end());
        add_issue(result, IssueSeverity::Warning, "unsupported_builtins",
                  "Unsupported or non-GLES2 built-ins: " + join(info.unsupported_builtins, ", "),
                  {}, "These are reported in the compatibility report; supported ones may be emulated.");
    }
    if (info.uses_texel_fetch) {
        add_issue(result, IssueSeverity::Warning, "texel_fetch",
                  "texelFetch is unavailable in GLSL ES 1.00 and needs a texture sampling rewrite.");
    }
    if (info.uses_texture_lod) {
        add_issue(result, IssueSeverity::Warning, "texture_lod",
                  "Explicit LOD sampling is not available in the fragment stage of GLSL ES 1.00 without extensions.");
    }
    if (info.uses_derivatives) {
        add_issue(result, IssueSeverity::Info, "derivatives",
                  "Derivative functions require GL_OES_standard_derivatives on GLES2.");
    }
    if (info.uses_switch) {
        add_issue(result, IssueSeverity::Warning, "switch_statement",
                  "switch is not part of GLSL ES 1.00 and must be lowered to if/else chains.");
    }
    if (info.uses_uint) {
        add_issue(result, IssueSeverity::Warning, "uint_type",
                  "Unsigned integer types are not available in GLSL ES 1.00.");
    }
    if (info.uses_double) {
        add_issue(result, IssueSeverity::Warning, "double_type",
                  "Double precision types are not available in GLSL ES 1.00.");
    }
    if (!info.optifine_uniforms.empty()) {
        std::sort(info.optifine_uniforms.begin(), info.optifine_uniforms.end());
        info.optifine_uniforms.erase(std::unique(info.optifine_uniforms.begin(), info.optifine_uniforms.end()),
                                     info.optifine_uniforms.end());
        add_issue(result, IssueSeverity::Info, "optifine_uniforms",
                  "OptiFine uniforms detected: " + join(info.optifine_uniforms, ", "),
                  {}, "The translation backend maps known names to NEXUS runtime uniforms.");
    }
    if (stage == ShaderStage::Fragment && info.outputs.size() > 1) {
        add_issue(result, IssueSeverity::Warning, "multiple_fragment_outputs",
                  "Multiple fragment outputs (MRT) are not supported by the MCPE GLES2 pipeline.");
    }

    bool has_error = false;
    bool has_warning = false;
    for (const auto& issue : result.issues) {
        if (issue.severity == IssueSeverity::Error) has_error = true;
        if (issue.severity == IssueSeverity::Warning) has_warning = true;
    }
    if (has_error) result.level = CompatibilityLevel::Unsupported;
    else if (has_warning || !info.optifine_uniforms.empty() || !info.unsupported_builtins.empty())
        result.level = CompatibilityLevel::Partial;
    else result.level = CompatibilityLevel::Full;

    return result;
}

nlohmann::json AnalysisResult::to_json() const {
    nlohmann::json j;
    j["level"] = compatibility_name(level);
    j["stage"] = stage_name(info.stage);
    j["version"] = info.version;
    j["es"] = info.es;
    j["has_main"] = info.has_main;
    j["function_count"] = info.function_count;
    j["global_count"] = info.global_count;
    j["features"] = {
        {"gl_frag_color", info.uses_frag_color},
        {"gl_frag_data", info.uses_frag_data},
        {"texel_fetch", info.uses_texel_fetch},
        {"texture_lod", info.uses_texture_lod},
        {"derivatives", info.uses_derivatives},
        {"discard", info.uses_discard},
        {"switch", info.uses_switch},
        {"uint", info.uses_uint},
        {"double", info.uses_double},
        {"ubo", info.uses_ubo}
    };
    j["uniforms"] = nlohmann::json::array();
    for (const auto& v : info.uniforms) {
        j["uniforms"].push_back({{"name", v.name}, {"type", v.type}, {"array_size", v.array_size}});
    }
    j["attributes"] = nlohmann::json::array();
    for (const auto& v : info.attributes) {
        j["attributes"].push_back({{"name", v.name}, {"type", v.type}});
    }
    j["varyings"] = nlohmann::json::array();
    for (const auto& v : info.varyings) {
        j["varyings"].push_back({{"name", v.name}, {"type", v.type}});
    }
    j["samplers"] = info.samplers;
    j["functions"] = info.functions;
    j["optifine_uniforms"] = info.optifine_uniforms;
    j["unsupported_builtins"] = info.unsupported_builtins;
    j["issues"] = nlohmann::json::array();
    for (const auto& issue : issues) {
        j["issues"].push_back({
            {"severity", severity_name(issue.severity)},
            {"code", issue.code},
            {"message", issue.message},
            {"line", issue.loc.line},
            {"hint", issue.hint}
        });
    }
    return j;
}

std::string AnalysisResult::to_markdown() const {
    std::ostringstream out;
    out << "# Shader compatibility report\n\n";
    out << "- Stage: `" << stage_name(info.stage) << "`\n";
    out << "- Source version: `" << info.version << (info.es ? " es" : "") << "`\n";
    out << "- Compatibility: **" << compatibility_name(level) << "**\n";
    out << "- Uniforms: " << info.uniforms.size() << ", attributes: " << info.attributes.size()
        << ", varyings: " << info.varyings.size() << "\n\n";
    if (!info.uniforms.empty()) {
        out << "## Uniforms\n\n";
        for (const auto& v : info.uniforms) out << "- `" << v.type << " " << v.name << "`\n";
        out << "\n";
    }
    if (!info.optifine_uniforms.empty()) {
        out << "## OptiFine uniforms detected\n\n";
        for (const auto& name : info.optifine_uniforms) out << "- `" << name << "`\n";
        out << "\n";
    }
    if (!issues.empty()) {
        out << "## Issues\n\n";
        for (const auto& issue : issues) {
            out << "- **" << severity_name(issue.severity) << "** (`" << issue.code << "`) line "
                << issue.loc.line << ": " << issue.message;
            if (!issue.hint.empty()) out << " — " << issue.hint;
            out << "\n";
        }
    }
    return out.str();
}

} // namespace nexus::shader
