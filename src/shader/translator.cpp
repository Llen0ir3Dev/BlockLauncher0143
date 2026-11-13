#include <nexus/shader/translator.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

#include <nexus/core/strings.hpp>
#include <nexus/shader/compiler.hpp>
#include <nexus/shader/preprocessor.hpp>

namespace nexus::shader {

const char* target_name(TargetProfile target) {
    switch (target) {
        case TargetProfile::GLES100: return "gles100";
        case TargetProfile::GLSL120: return "glsl120";
        case TargetProfile::GLSLES300: return "gles300";
    }
    return "gles100";
}

const char* quality_name(QualityProfile quality) {
    switch (quality) {
        case QualityProfile::Performance: return "performance";
        case QualityProfile::Balanced: return "balanced";
        case QualityProfile::Quality: return "quality";
    }
    return "balanced";
}

bool parse_target(const std::string& text, TargetProfile& out) {
    const std::string lower = to_lower(text);
    if (lower == "gles100" || lower == "gles2" || lower == "es100" || lower == "glsl100") {
        out = TargetProfile::GLES100; return true;
    }
    if (lower == "glsl120" || lower == "gl120" || lower == "desktop120") {
        out = TargetProfile::GLSL120; return true;
    }
    if (lower == "gles300" || lower == "gles3" || lower == "es300") {
        out = TargetProfile::GLSLES300; return true;
    }
    return false;
}

bool parse_quality(const std::string& text, QualityProfile& out) {
    const std::string lower = to_lower(text);
    if (lower == "performance" || lower == "fast") { out = QualityProfile::Performance; return true; }
    if (lower == "balanced" || lower == "default") { out = QualityProfile::Balanced; return true; }
    if (lower == "quality" || lower == "high") { out = QualityProfile::Quality; return true; }
    return false;
}

const std::vector<UniformMapping>& optifine_uniform_map() {
    static const std::vector<UniformMapping> map = {
        {"gbufferModelView", "u_modelView", "camera model-view matrix", true},
        {"gbufferModelViewInverse", "u_modelViewInverse", "inverse camera model-view matrix", true},
        {"gbufferProjection", "u_projection", "camera projection matrix", true},
        {"gbufferProjectionInverse", "u_projectionInverse", "inverse camera projection matrix", true},
        {"gbufferPreviousModelView", "u_previousModelView", "previous frame model-view matrix", false},
        {"gbufferPreviousProjection", "u_previousProjection", "previous frame projection matrix", false},
        {"shadowModelView", "u_shadowModelView", "shadow model-view matrix", true},
        {"shadowModelViewInverse", "u_shadowModelViewInverse", "inverse shadow model-view matrix", false},
        {"shadowProjection", "u_shadowProjection", "shadow projection matrix", true},
        {"shadowProjectionInverse", "u_shadowProjectionInverse", "inverse shadow projection matrix", false},
        {"sunPosition", "u_sunPosition", "sun direction in view space", true},
        {"moonPosition", "u_moonPosition", "moon direction in view space", true},
        {"shadowLightPosition", "u_shadowLightPosition", "shadow light direction", true},
        {"cameraPosition", "u_cameraPosition", "camera position in world space", true},
        {"previousCameraPosition", "u_previousCameraPosition", "previous camera position", false},
        {"frameTime", "u_frameTime", "frame delta in seconds", true},
        {"frameCounter", "u_frameCounter", "frame counter", true},
        {"worldTime", "u_worldTime", "world time in ticks", true},
        {"worldDay", "u_worldDay", "world day", true},
        {"rainStrength", "u_rainStrength", "rain strength 0..1", true},
        {"wetness", "u_wetness", "wetness 0..1", true},
        {"viewWidth", "u_viewWidth", "viewport width", true},
        {"viewHeight", "u_viewHeight", "viewport height", true},
        {"near", "u_near", "near plane", true},
        {"far", "u_far", "far plane", true},
        {"aspectRatio", "u_aspectRatio", "viewport aspect ratio", true},
        {"centerDepthSmooth", "u_centerDepthSmooth", "smoothed centre depth", false},
        {"isEyeInWater", "u_isEyeInWater", "eye-in-water flag", true},
        {"nightVision", "u_nightVision", "night vision strength", true},
        {"blindness", "u_blindness", "blindness strength", true},
        {"screenBrightness", "u_screenBrightness", "brightness setting", true},
        {"hideGUI", "u_hideGUI", "GUI visibility flag", false},
        {"fogColor", "u_fogColor", "fog colour", true},
        {"skyColor", "u_skyColor", "sky colour", true},
        {"sunColor", "u_sunColor", "sun colour", true},
        {"moonColor", "u_moonColor", "moon colour", true},
        {"upColor", "u_upColor", "upward ambient colour", true},
        {"eyeBrightness", "u_eyeBrightness", "eye brightness", false},
        {"eyeBrightnessSmooth", "u_eyeBrightnessSmooth", "smoothed eye brightness", false},
        {"atlasSize", "u_atlasSize", "texture atlas size", true},
        {"heldItemId", "u_heldItemId", "held item id", false},
        {"heldBlockLightValue", "u_heldBlockLightValue", "held block light", false},
        {"entityId", "u_entityId", "entity id", false},
        {"entityColor", "u_entityColor", "entity colour", false},
        {"blockEntityId", "u_blockEntityId", "block entity id", false}
    };
    return map;
}

namespace {

void add_issue(TranslationResult& result, IssueSeverity severity, const std::string& code,
               const std::string& message, const SourceLoc& loc = {}, const std::string& hint = "") {
    Issue issue;
    issue.severity = severity;
    issue.code = code;
    issue.message = message;
    issue.loc = loc;
    issue.hint = hint;
    result.issues.push_back(std::move(issue));
}

struct Codegen {
    std::ostringstream out;
    int indent = 0;
    ShaderStage stage = ShaderStage::Unknown;
    TargetProfile target = TargetProfile::GLES100;
    TranslationOptions options;
    std::map<std::string, std::string> name_map;
    std::set<std::string> helpers;
    bool fragment_output_mapped = false;
    std::string fragment_output_name;
    TranslationResult* result = nullptr;

    void line(const std::string& text) {
        out << std::string(static_cast<size_t>(indent) * 4, ' ') << text << "\n";
    }
    std::string pad() const { return std::string(static_cast<size_t>(indent) * 4, ' '); }

    std::string map_name(const std::string& name) const {
        const auto it = name_map.find(name);
        return it == name_map.end() ? name : it->second;
    }

    std::string emit_expr(const Expr& expr);
    void emit_statement(const Stmt& stmt);
    void emit_var_decl(const VarDecl& var);
    void emit_function(const FunctionDecl& fn);
    void emit_struct(const StructDecl& s);
    void emit_precision();
    void emit_helpers();
};

[[maybe_unused]] bool is_sampler_type(const std::string& type) {
    return type.rfind("sampler", 0) == 0 || type.rfind("isampler", 0) == 0 || type.rfind("usampler", 0) == 0;
}

std::string strip_float_suffix(std::string literal) {
    if (!literal.empty() && (literal.back() == 'f' || literal.back() == 'F')) literal.pop_back();
    if (!literal.empty() && (literal.back() == 'u' || literal.back() == 'U')) literal.pop_back();
    return literal;
}

std::string target_qualifier(const TypeSpec& type, ShaderStage stage, TargetProfile target) {
    if (type.has_qualifier("uniform")) return "uniform";
    if (type.has_qualifier("const")) return "const";
    if (type.has_qualifier("attribute")) return target == TargetProfile::GLSLES300 ? "in" : "attribute";
    if (type.has_qualifier("varying")) {
        if (target == TargetProfile::GLSLES300) return stage == ShaderStage::Vertex ? "out" : "in";
        return "varying";
    }
    if (type.has_qualifier("in")) {
        if (target == TargetProfile::GLSLES300) return "in";
        return stage == ShaderStage::Vertex ? "attribute" : "varying";
    }
    if (type.has_qualifier("out")) {
        if (target == TargetProfile::GLSLES300) return "out";
        return stage == ShaderStage::Vertex ? "varying" : "out";
    }
    if (type.has_qualifier("inout")) return "inout";
    return "";
}

std::string clean_type(const TypeSpec& type) {
    return type.base;
}

std::string emit_array_dims(const TypeSpec& type) {
    std::string out;
    for (const auto& dim : type.array_dims) {
        if (!dim.empty()) out += "[" + dim + "]";
    }
    return out;
}

void collect_identifiers(const Expr& expr, std::set<std::string>& names) {
    if (expr.kind == Expr::Kind::Identifier) names.insert(expr.value);
    for (const auto& arg : expr.args) if (arg) collect_identifiers(*arg, names);
}

void collect_statement_identifiers(const Stmt& stmt, std::set<std::string>& names) {
    if (stmt.expr) collect_identifiers(*stmt.expr, names);
    if (stmt.cond) collect_identifiers(*stmt.cond, names);
    if (stmt.init_expr) collect_identifiers(*stmt.init_expr, names);
    if (stmt.incr) collect_identifiers(*stmt.incr, names);
    for (const auto& decl : stmt.decls) if (decl && decl->init) collect_identifiers(*decl->init, names);
    for (const auto& child : stmt.body) if (child) collect_statement_identifiers(*child, names);
    if (stmt.else_branch) collect_statement_identifiers(*stmt.else_branch, names);
}

std::string Codegen::emit_expr(const Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::IntLiteral:
            return expr.value;
        case Expr::Kind::FloatLiteral:
            return strip_float_suffix(expr.value);
        case Expr::Kind::BoolLiteral:
            return expr.value;
        case Expr::Kind::Identifier:
            if (expr.value == "gl_FragData" && fragment_output_mapped) return "gl_FragColor";
            return map_name(expr.value);
        case Expr::Kind::Unary:
            return expr.op + emit_expr(*expr.args[0]);
        case Expr::Kind::Postfix:
            return emit_expr(*expr.args[0]) + expr.op;
        case Expr::Kind::Binary:
            return "(" + emit_expr(*expr.args[0]) + " " + expr.op + " " + emit_expr(*expr.args[1]) + ")";
        case Expr::Kind::Assign:
            return emit_expr(*expr.args[0]) + " " + expr.op + " " + emit_expr(*expr.args[1]);
        case Expr::Kind::Ternary:
            return "(" + emit_expr(*expr.args[0]) + " ? " + emit_expr(*expr.args[1]) + " : " +
                   emit_expr(*expr.args[2]) + ")";
        case Expr::Kind::Index:
            if (expr.args[0]->kind == Expr::Kind::Identifier &&
                expr.args[0]->value == "gl_FragData" && fragment_output_mapped) {
                if (expr.args[1]->kind == Expr::Kind::IntLiteral && expr.args[1]->value == "0") {
                    return "gl_FragColor";
                }
                add_issue(*result, IssueSeverity::Warning, "frag_data_index",
                          "gl_FragData with a non-zero index is not supported by GLES2; using gl_FragColor.",
                          expr.loc);
                return "gl_FragColor";
            }
            return emit_expr(*expr.args[0]) + "[" + emit_expr(*expr.args[1]) + "]";
        case Expr::Kind::Field:
            return emit_expr(*expr.args[0]) + "." + expr.value;
        case Expr::Kind::Paren:
            return "(" + emit_expr(*expr.args[0]) + ")";
        case Expr::Kind::Comma: {
            std::string out;
            for (size_t i = 0; i < expr.args.size(); ++i) {
                if (i) out += ", ";
                out += emit_expr(*expr.args[i]);
            }
            return out;
        }
        case Expr::Kind::Call: {
            std::string callee = expr.value.empty() ? "" : map_name(expr.value);
            std::string args;
            const size_t start = expr.value.empty() ? 1 : 0;
            for (size_t i = start; i < expr.args.size(); ++i) {
                if (i > start) args += ", ";
                args += emit_expr(*expr.args[i]);
            }
            if (expr.value.empty()) {
                // Constructor through a parenthesised expression is not valid GLSL;
                // preserve the expression to keep diagnostics useful.
                return emit_expr(*expr.args[0]) + "(" + args + ")";
            }
            if ((callee == "texture" || callee == "texture2D") && target != TargetProfile::GLSLES300) {
                // GLSL ES 1.00 and GLSL 1.20 use texture2D for sampler2D.
                callee = "texture2D";
            } else if (callee == "textureLod") {
                if (target != TargetProfile::GLSLES300) {
                    add_issue(*result, IssueSeverity::Warning, "texture_lod_lowered",
                              "textureLod: explicit LOD argument dropped for GLES2 compatibility.", expr.loc);
                    callee = "texture2D";
                    // Drop the LOD argument (third argument).
                    std::string trimmed;
                    size_t commas = 0;
                    for (size_t i = 0; i < args.size(); ++i) {
                        if (args[i] == ',' && ++commas == 2) break;
                        trimmed += args[i];
                    }
                    args = trimmed;
                }
            } else if (callee == "textureGrad" || callee == "textureGradOffset") {
                add_issue(*result, IssueSeverity::Warning, "texture_grad_lowered",
                          "textureGrad: gradients dropped; result is approximate.", expr.loc);
                callee = "texture2D";
                std::string trimmed;
                size_t commas = 0;
                for (size_t i = 0; i < args.size(); ++i) {
                    if (args[i] == ',' && ++commas == 2) break;
                    trimmed += args[i];
                }
                args = trimmed;
            } else if (callee == "texelFetch") {
                add_issue(*result, IssueSeverity::Warning, "texel_fetch_lowered",
                          "texelFetch translated to texture2D; integer coordinates are not normalised.", expr.loc);
                callee = "texture2D";
            } else if (callee == "round") {
                helpers.insert("round");
                callee = "nexus_round";
            } else if (callee == "trunc") {
                helpers.insert("trunc");
                callee = "nexus_trunc";
            } else if (callee == "saturate") {
                return "clamp(" + args + ", 0.0, 1.0)";
            } else if (callee == "inverse" || callee == "determinant" || callee == "transpose") {
                add_issue(*result, IssueSeverity::Warning, "unsupported_matrix_function",
                          callee + " is not available in GLSL ES 1.00.", expr.loc);
            }
            return callee + "(" + args + ")";
        }
    }
    return "";
}

void Codegen::emit_var_decl(const VarDecl& var) {
    const std::string qualifier = target_qualifier(var.type, stage, target);
    std::string type = clean_type(var.type);
    const std::string name = map_name(var.name);
    std::string line;
    if (!qualifier.empty()) line += qualifier + " ";
    line += type + " " + name + emit_array_dims(var.type);
    if (var.init) line += " = " + emit_expr(*var.init);
    line += ";";
    this->line(line);
}

void Codegen::emit_struct(const StructDecl& s) {
    line("struct " + s.name + " {");
    ++indent;
    for (const auto& field : s.fields) {
        line(clean_type(field.type) + " " + field.name + emit_array_dims(field.type) + ";");
    }
    --indent;
    line("};");
}

void Codegen::emit_statement(const Stmt& stmt) {
    switch (stmt.kind) {
        case Stmt::Kind::Empty:
            line(";");
            break;
        case Stmt::Kind::Expr:
            line(emit_expr(*stmt.expr) + ";");
            break;
        case Stmt::Kind::Declaration:
            for (const auto& decl : stmt.decls) emit_var_decl(*decl);
            break;
        case Stmt::Kind::Block:
            line("{");
            ++indent;
            for (const auto& child : stmt.body) emit_statement(*child);
            --indent;
            line("}");
            break;
        case Stmt::Kind::If:
            line("if (" + emit_expr(*stmt.cond) + ")");
            if (!stmt.body.empty()) emit_statement(*stmt.body[0]);
            if (stmt.else_branch) {
                line("else");
                emit_statement(*stmt.else_branch);
            }
            break;
        case Stmt::Kind::For: {
            std::string init;
            for (const auto& decl : stmt.decls) {
                const std::string qualifier = target_qualifier(decl->type, stage, target);
                if (!qualifier.empty()) init += qualifier + " ";
                init += clean_type(decl->type) + " " + map_name(decl->name) + emit_array_dims(decl->type);
                if (decl->init) init += " = " + emit_expr(*decl->init);
            }
            if (stmt.init_expr) init += emit_expr(*stmt.init_expr);
            const std::string cond = stmt.cond ? emit_expr(*stmt.cond) : "";
            const std::string incr = stmt.incr ? emit_expr(*stmt.incr) : "";
            line("for (" + init + "; " + cond + "; " + incr + ")");
            if (!stmt.body.empty()) emit_statement(*stmt.body[0]);
            break;
        }
        case Stmt::Kind::While:
            line("while (" + emit_expr(*stmt.cond) + ")");
            if (!stmt.body.empty()) emit_statement(*stmt.body[0]);
            break;
        case Stmt::Kind::DoWhile:
            line("do");
            if (!stmt.body.empty()) emit_statement(*stmt.body[0]);
            line("while (" + emit_expr(*stmt.cond) + ");");
            break;
        case Stmt::Kind::Return:
            line("return" + (stmt.expr ? " " + emit_expr(*stmt.expr) : std::string()) + ";");
            break;
        case Stmt::Kind::Break: line("break;"); break;
        case Stmt::Kind::Continue: line("continue;"); break;
        case Stmt::Kind::Discard: line("discard;"); break;
        case Stmt::Kind::Switch:
            line("// NEXUS: switch is not available in GLSL ES 1.00");
            line("switch (" + emit_expr(*stmt.cond) + ") {");
            ++indent;
            for (const auto& child : stmt.body) emit_statement(*child);
            --indent;
            line("}");
            break;
        case Stmt::Kind::Case:
            line("case " + emit_expr(*stmt.expr) + ":");
            break;
        case Stmt::Kind::Default:
            line("default:");
            break;
    }
}

void Codegen::emit_function(const FunctionDecl& fn) {
    std::string signature = clean_type(fn.return_type) + " " + map_name(fn.name) + "(";
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i) signature += ", ";
        std::string qualifier;
        if (fn.params[i].type.has_qualifier("const")) qualifier = "const ";
        else if (fn.params[i].type.has_qualifier("inout")) qualifier = "inout ";
        else if (fn.params[i].type.has_qualifier("out")) qualifier = "out ";
        else if (fn.params[i].type.has_qualifier("in")) qualifier = "in ";
        signature += qualifier;
        signature += clean_type(fn.params[i].type);
        if (!fn.params[i].name.empty()) signature += " " + map_name(fn.params[i].name);
        signature += emit_array_dims(fn.params[i].type);
    }
    signature += ")";
    if (fn.prototype || !fn.body) {
        line(signature + ";");
        return;
    }
    line(signature + " {");
    ++indent;
    for (const auto& stmt : fn.body->body) emit_statement(*stmt);
    --indent;
    line("}");
}

void Codegen::emit_precision() {
    // Precision qualifiers are not part of desktop GLSL 1.20; injecting them
    // makes otherwise valid output fail on desktop compatibility contexts.
    if (target != TargetProfile::GLES100) return;
    if (!options.inject_precision) return;
    const char* float_precision = options.quality == QualityProfile::Performance ? "mediump" : "highp";
    line(std::string("precision ") + float_precision + " float;");
    line("precision highp int;");
}

void Codegen::emit_helpers() {
    if (helpers.count("round")) {
        line("float nexus_round(float x) { return floor(x + 0.5); }");
        line("vec2 nexus_round(vec2 x) { return floor(x + vec2(0.5)); }");
        line("vec3 nexus_round(vec3 x) { return floor(x + vec3(0.5)); }");
        line("vec4 nexus_round(vec4 x) { return floor(x + vec4(0.5)); }");
    }
    if (helpers.count("trunc")) {
        line("float nexus_trunc(float x) { return sign(x) * floor(abs(x)); }");
        line("vec2 nexus_trunc(vec2 x) { return sign(x) * floor(abs(x)); }");
        line("vec3 nexus_trunc(vec3 x) { return sign(x) * floor(abs(x)); }");
        line("vec4 nexus_trunc(vec4 x) { return sign(x) * floor(abs(x)); }");
    }
}

} // namespace

TranslationResult translate_unit(const TranslationUnit& unit, ShaderStage stage,
                                 const TranslationOptions& options) {
    TranslationResult result;
    const AnalysisResult analysis = analyze_shader(unit, stage);
    result.issues = analysis.issues;
    result.level = analysis.level;

    Codegen gen;
    gen.stage = stage;
    gen.target = options.target;
    gen.options = options;
    gen.result = &result;

    // Uniform mapping.
    for (const auto& global : unit.globals) {
        if (!global.type.has_qualifier("uniform")) continue;
        auto it = std::find_if(optifine_uniform_map().begin(), optifine_uniform_map().end(),
                               [&](const UniformMapping& m) { return m.source == global.name; });
        if (it != optifine_uniform_map().end() && options.map_optifine_uniforms) {
            gen.name_map[global.name] = it->target;
            result.uniform_map.push_back(*it);
            if (!it->supported) {
                add_issue(result, IssueSeverity::Warning, "optifine_uniform_unbound",
                          "OptiFine uniform '" + global.name + "' has no runtime binding.", global.loc,
                          "A zero/default value will be supplied; the shader is PARTIAL.");
            }
        }
    }

    // Fragment output mapping.
    if (stage == ShaderStage::Fragment && options.target != TargetProfile::GLSLES300) {
        for (const auto& global : unit.globals) {
            if (global.type.has_qualifier("out") && !global.type.has_qualifier("uniform")) {
                gen.fragment_output_mapped = true;
                gen.fragment_output_name = global.name;
                gen.name_map[global.name] = "gl_FragColor";
                if (unit.version >= 300) {
                    add_issue(result, IssueSeverity::Info, "fragment_output_mapped",
                              "Fragment output '" + global.name + "' mapped to gl_FragColor.", global.loc);
                }
                break;
            }
        }
    }

    // Detect desktop built-ins that need compatibility declarations.
    std::set<std::string> identifiers;
    for (const auto& global : unit.globals) {
        if (global.init) collect_identifiers(*global.init, identifiers);
    }
    for (const auto& fn : unit.functions) {
        if (fn.body) collect_statement_identifiers(*fn.body, identifiers);
    }
    std::set<std::string> builtin_declarations;
    auto need = [&](const std::string& name, const std::string& declaration, const std::string& replacement) {
        if (identifiers.count(name)) {
            builtin_declarations.insert(declaration);
            gen.name_map[name] = replacement;
        }
    };
    const bool legacy = options.target != TargetProfile::GLSLES300;
    need("gl_Vertex", legacy ? "attribute vec4 a_vertex;" : "in vec4 a_vertex;", "a_vertex");
    need("gl_Normal", legacy ? "attribute vec3 a_normal;" : "in vec3 a_normal;", "a_normal");
    need("gl_MultiTexCoord0", legacy ? "attribute vec4 a_texcoord0;" : "in vec4 a_texcoord0;", "a_texcoord0");
    need("gl_Color", legacy ? "attribute vec4 a_color;" : "in vec4 a_color;", "a_color");
    need("gl_ModelViewMatrix", "uniform mat4 u_modelView;", "u_modelView");
    need("gl_ProjectionMatrix", "uniform mat4 u_projection;", "u_projection");
    need("gl_ModelViewProjectionMatrix", "uniform mat4 u_modelViewProjection;", "u_modelViewProjection");
    need("gl_NormalMatrix", "uniform mat3 u_normalMatrix;", "u_normalMatrix");
    need("gl_TextureMatrix", "uniform mat4 u_textureMatrix[8];", "u_textureMatrix");
    need("gl_TexCoord", legacy ? "varying vec4 v_texcoord[8];" : "in vec4 v_texcoord[8];", "v_texcoord");
    need("gl_FrontColor", legacy ? "varying vec4 v_frontColor;" : "out vec4 v_frontColor;", "v_frontColor");
    need("gl_FragColor", "", "gl_FragColor");

    // Extensions.
    if (analysis.info.uses_derivatives && options.target == TargetProfile::GLES100 &&
        options.add_derivative_extension) {
        result.required_extensions.push_back("GL_OES_standard_derivatives");
        add_issue(result, IssueSeverity::Info, "extension_added",
                  "Added GL_OES_standard_derivatives for derivative functions.");
    }
    if (!analysis.info.optifine_uniforms.empty()) {
        add_issue(result, IssueSeverity::Info, "optifine_runtime_bindings",
                  "OptiFine uniforms are bound by the NEXUS renderer; unmapped names use defaults.");
    }

    // Header.
    if (options.target == TargetProfile::GLES100) gen.line("#version 100");
    else if (options.target == TargetProfile::GLSL120) gen.line("#version 120");
    else gen.line("#version 300 es");
    for (const auto& ext : result.required_extensions) {
        gen.line("#extension " + ext + " : enable");
    }
    gen.emit_precision();
    for (const auto& decl : builtin_declarations) {
        if (!decl.empty()) gen.line(decl);
    }
    gen.line("");

    for (const auto& s : unit.structs) gen.emit_struct(s);
    if (!unit.structs.empty()) gen.line("");

    // Globals, skipping the fragment output variable that became gl_FragColor.
    for (const auto& global : unit.globals) {
        if (stage == ShaderStage::Fragment && global.name == gen.fragment_output_name &&
            options.target != TargetProfile::GLSLES300) {
            continue;
        }
        // GLSL ES 1.00 has no array initialisers for uniforms in this pipeline;
        // keep the declaration and let the validator report if it is illegal.
        gen.emit_var_decl(global);
    }
    if (!unit.globals.empty()) gen.line("");

    for (const auto& fn : unit.functions) {
        gen.emit_function(fn);
        gen.line("");
    }
    gen.emit_helpers();

    // Ensure the helper functions are placed before main() by rebuilding if needed.
    std::string source = gen.out.str();
    if (!gen.helpers.empty()) {
        // Helpers were appended after functions; move them before the first
        // non-preprocessor, non-declaration function definition.
        std::ostringstream rebuilt;
        std::istringstream in(source);
        std::string line;
        bool inserted = false;
        while (std::getline(in, line)) {
            if (!inserted && line.rfind("void main", 0) == 0) {
                std::ostringstream helpers;
                Codegen h;
                h.target = options.target;
                h.options = options;
                h.helpers = gen.helpers;
                h.result = &result;
                h.emit_helpers();
                rebuilt << helpers.str();
                inserted = true;
            }
            rebuilt << line << "\n";
        }
        if (!inserted) {
            std::ostringstream helpers;
            Codegen h;
            h.target = options.target;
            h.options = options;
            h.helpers = gen.helpers;
            h.result = &result;
            h.emit_helpers();
            rebuilt << helpers.str();
        }
        source = rebuilt.str();
    }
    result.source = source;

    if (options.validate_structure) {
        const ParseResult check = parse_glsl(source, ParseOptions{stage});
        if (!check.success) {
            add_issue(result, IssueSeverity::Error, "generated_parse_error",
                      "Generated GLSL does not parse: " + check.error);
            result.success = false;
            result.error = check.error;
        } else {
            result.success = true;
        }
    } else {
        result.success = true;
    }

    // Recompute level if new issues were added.
    bool has_error = false;
    bool has_warning = false;
    for (const auto& issue : result.issues) {
        if (issue.severity == IssueSeverity::Error) has_error = true;
        if (issue.severity == IssueSeverity::Warning) has_warning = true;
    }
    if (has_error) result.level = CompatibilityLevel::Unsupported;
    else if (has_warning) result.level = CompatibilityLevel::Partial;

    result.report = {
        {"target", target_name(options.target)},
        {"quality", quality_name(options.quality)},
        {"stage", stage_name(stage)},
        {"compatibility", compatibility_name(result.level)},
        {"success", result.success},
        {"uniform_map", nlohmann::json::array()},
        {"required_extensions", result.required_extensions},
        {"issues", nlohmann::json::array()}
    };
    for (const auto& m : result.uniform_map) {
        result.report["uniform_map"].push_back({
            {"source", m.source}, {"target", m.target}, {"supported", m.supported}, {"note", m.note}
        });
    }
    for (const auto& issue : result.issues) {
        result.report["issues"].push_back({
            {"severity", severity_name(issue.severity)},
            {"code", issue.code}, {"message", issue.message}, {"line", issue.loc.line}
        });
    }
    return result;
}

TranslationResult translate_glsl(const std::string& source, ShaderStage stage,
                                 const TranslationOptions& options) {
    const PreprocessResult pre = preprocess_glsl(source, PreprocessOptions{stage, {}, false});
    if (!pre.success) {
        TranslationResult result;
        result.error = pre.error;
        result.success = false;
        add_issue(result, IssueSeverity::Error, "preprocess_error", pre.error);
        return result;
    }
    ParseOptions parse_options;
    parse_options.stage = stage;
    ParseResult parsed = parse_glsl(pre.source, parse_options);
    if (!parsed.success) {
        TranslationResult result;
        result.error = parsed.error;
        result.success = false;
        add_issue(result, IssueSeverity::Error, "parse_error", parsed.error);
        return result;
    }
    parsed.unit.version = pre.version;
    parsed.unit.es = pre.es;
    parsed.unit.directives.insert(parsed.unit.directives.end(), pre.directives.begin(), pre.directives.end());
    parsed.unit.extensions.insert(parsed.unit.extensions.end(), pre.extensions.begin(), pre.extensions.end());
    TranslationResult result = translate_unit(parsed.unit, stage, options);
    for (const auto& warning : pre.warnings) {
        add_issue(result, IssueSeverity::Info, "preprocess_warning", warning);
    }
    return result;
}

} // namespace nexus::shader
