#include <nexus/shader/pack.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/zip.hpp>
#include <nexus/shader/preprocessor.hpp>

namespace nexus::shader {

const ShaderProgram* ShaderPack::find_program(const std::string& name) const {
    for (const auto& program : programs) {
        if (program.name == name) return &program;
    }
    return nullptr;
}

std::vector<std::string> ShaderPack::program_names() const {
    std::vector<std::string> names;
    for (const auto& program : programs) names.push_back(program.name);
    return names;
}

namespace {

bool is_shader_text_file(const std::string& path) {
    const std::string ext = extension(path);
    return ext == ".vsh" || ext == ".fsh" || ext == ".vert" || ext == ".frag" ||
           ext == ".glsl" || ext == ".inc" || ext == ".properties" || ext == ".txt" ||
           ext == ".cfg" || ext == ".json" || ext == ".lang";
}

ShaderStage detect_stage(const std::string& path) {
    const std::string ext = extension(path);
    if (ext == ".vsh" || ext == ".vert") return ShaderStage::Vertex;
    if (ext == ".fsh" || ext == ".frag") return ShaderStage::Fragment;
    const std::string lower = to_lower(path);
    if (lower.find("vertex") != std::string::npos || lower.find(".vsh") != std::string::npos) return ShaderStage::Vertex;
    if (lower.find("fragment") != std::string::npos || lower.find(".fsh") != std::string::npos) return ShaderStage::Fragment;
    return ShaderStage::Unknown;
}

std::string normalize_virtual(const std::string& path) {
    std::string out = normalize_slashes(path);
    while (!out.empty() && out.front() == '/') out.erase(out.begin());
    return out;
}

std::string relative_to(const std::string& path, const std::string& prefix) {
    if (!prefix.empty() && path.compare(0, prefix.size(), prefix) == 0) return path.substr(prefix.size());
    return path;
}

bool read_text_file(const std::string& path, std::string& out, uint64_t max_bytes) {
    if (!fs::read_file(path, out, max_bytes)) return false;
    return true;
}

void parse_properties(const std::string& text, std::map<std::string, std::string>& out) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == '!') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (!key.empty()) out[key] = value;
    }
}

struct IncludeResolver {
    const std::map<std::string, std::string>& files;
    const std::string& prefix;
    size_t max_depth;
    std::vector<std::string> issues;

    std::string resolve(const std::string& source, const std::string& current_path,
                        std::vector<std::string>& includes, size_t depth) {
        if (depth > max_depth) {
            issues.push_back("include depth exceeded at " + current_path);
            return source;
        }
        std::ostringstream out;
        std::istringstream in(source);
        std::string line;
        while (std::getline(in, line)) {
            std::string stripped = trim(line);
            if (starts_with(stripped, "#include")) {
                const size_t first_quote = stripped.find_first_of("\"<");
                const size_t last_quote = stripped.find_last_of("\">");
                if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
                    const std::string include_name = stripped.substr(first_quote + 1, last_quote - first_quote - 1);
                    std::vector<std::string> candidates;
                    candidates.push_back(normalize_virtual(join_path(dirname(current_path), include_name)));
                    candidates.push_back(normalize_virtual(prefix + include_name));
                    candidates.push_back(normalize_virtual(prefix + "shaders/" + include_name));
                    candidates.push_back(normalize_virtual("shaders/" + include_name));
                    candidates.push_back(normalize_virtual(include_name));
                    const std::string* found = nullptr;
                    std::string found_path;
                    for (const auto& candidate : candidates) {
                        const auto it = files.find(candidate);
                        if (it != files.end()) {
                            found = &it->second;
                            found_path = candidate;
                            break;
                        }
                    }
                    if (!found) {
                        // Fall back to a unique basename match.
                        const std::string base = basename(include_name);
                        std::vector<std::string> matches;
                        for (const auto& item : files) {
                            if (basename(item.first) == base) matches.push_back(item.first);
                        }
                        if (matches.size() == 1) {
                            found_path = matches[0];
                            found = &files.at(found_path);
                        }
                    }
                    if (found) {
                        includes.push_back(found_path);
                        out << resolve(*found, found_path, includes, depth + 1);
                        if (out.tellp() > 0) out << "\n";
                    } else {
                        issues.push_back("unresolved include '" + include_name + "' in " + current_path);
                        out << line << "\n";
                    }
                    continue;
                }
            }
            out << line << "\n";
        }
        return out.str();
    }

    static std::string join_path(const std::string& a, const std::string& b) {
        if (a.empty() || a == ".") return b;
        return a + "/" + b;
    }
};

std::string sanitize_program_name(const std::string& name) {
    std::string out = name;
    for (char& c : out) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') c = '_';
    }
    return out;
}

AnalysisResult analyze_program_source(const std::string& source, ShaderStage stage) {
    PreprocessOptions popts;
    popts.stage = stage;
    PreprocessResult pre = preprocess_glsl(source, popts);
    if (!pre.success) {
        AnalysisResult result;
        Issue issue;
        issue.severity = IssueSeverity::Error;
        issue.code = "preprocess_error";
        issue.message = pre.error;
        result.issues.push_back(issue);
        result.level = CompatibilityLevel::Unsupported;
        result.info.stage = stage;
        return result;
    }
    ParseOptions options;
    options.stage = stage;
    ParseResult parsed = parse_glsl(pre.source, options);
    if (!parsed.success) {
        AnalysisResult result;
        Issue issue;
        issue.severity = IssueSeverity::Error;
        issue.code = "parse_error";
        issue.message = parsed.error;
        result.issues.push_back(issue);
        result.level = CompatibilityLevel::Unsupported;
        result.info.stage = stage;
        return result;
    }
    parsed.unit.version = pre.version;
    parsed.unit.es = pre.es;
    return analyze_shader(parsed.unit, stage);
}

} // namespace

ShaderPack load_shader_pack(const std::string& path, const PackLoadOptions& options, std::string* error) {
    ShaderPack pack;
    pack.root = path;
    const std::string ext = extension(path);
    pack.archive = (ext == ".zip");

    std::map<std::string, std::string> raw_files;
    if (pack.archive) {
        ZipReader zip;
        std::string zip_error;
        if (!zip.open(path, zip_error)) {
            if (error) *error = zip_error;
            return pack;
        }
        for (const auto& entry : zip.entries()) {
            if (entry.is_directory) continue;
            if (!is_shader_text_file(entry.name)) continue;
            if (entry.uncompressed_size > options.max_file_bytes) continue;
            std::string text;
            std::string read_error;
            if (zip.read_text(entry.name, text, read_error, options.max_file_bytes)) {
                raw_files[normalize_virtual(entry.name)] = std::move(text);
            } else {
                pack.issues.push_back(read_error);
            }
        }
    } else if (fs::is_directory(path)) {
        for (const auto& file : fs::list_directory(path, true)) {
            if (!is_shader_text_file(file)) continue;
            if (fs::file_size(file) > options.max_file_bytes) continue;
            std::string text;
            if (read_text_file(file, text, options.max_file_bytes)) {
                std::string relative = normalize_slashes(file);
                const std::string root = normalize_slashes(path);
                if (relative.compare(0, root.size(), root) == 0) {
                    relative = relative.substr(root.size());
                    while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
                }
                raw_files[normalize_virtual(relative)] = std::move(text);
            }
        }
    } else {
        if (error) *error = "shader pack is neither a directory nor a .zip archive: " + path;
        return pack;
    }

    if (raw_files.empty()) {
        if (error) *error = "no shader source files found in " + path;
        return pack;
    }

    std::string prefix;
    for (const auto& item : raw_files) {
        const size_t pos = item.first.find("shaders/");
        if (pos != std::string::npos) {
            prefix = item.first.substr(0, pos + 8);
            break;
        }
    }
    if (prefix.empty()) prefix = "";
    pack.shaders_prefix = prefix;
    pack.name = basename(path);

    // properties
    for (const auto& item : raw_files) {
        if (ends_with(item.first, "shaders.properties")) {
            parse_properties(item.second, pack.properties);
        }
    }

    // Detect format.
    bool has_optifine_markers = false;
    for (const auto& item : raw_files) {
        const std::string name = to_lower(item.first);
        if (name.find("gbuffers_") != std::string::npos || name.find("composite") != std::string::npos ||
            name.find("shadow") != std::string::npos || name.find("deferred") != std::string::npos) {
            has_optifine_markers = true;
            break;
        }
    }
    if (has_optifine_markers) pack.format = PackFormat::OptiFine;
    else if (!raw_files.empty()) pack.format = PackFormat::Java;

    IncludeResolver resolver{raw_files, prefix, options.max_include_depth, {}};
    size_t total = 0;
    for (const auto& item : raw_files) {
        total += item.second.size();
        if (total > options.max_total_bytes) {
            pack.issues.push_back("shader pack exceeds total size limit");
            break;
        }
        pack.files[item.first] = ShaderFile{item.first, item.second, pack.archive};
        const ShaderStage stage = detect_stage(item.first);
        if (stage == ShaderStage::Unknown) continue;
        if (ends_with(item.first, ".glsl") || ends_with(item.first, ".inc")) continue;
        ShaderProgram program;
        program.name = relative_to(item.first, prefix);
        program.virtual_path = item.first;
        program.stage = stage;
        program.source = resolver.resolve(item.second, item.first, program.includes, 0);
        program.resolved = program.source.find("#include") == std::string::npos;
        if (!program.resolved) program.error = "one or more includes could not be resolved";
        pack.programs.push_back(std::move(program));
    }
    for (const auto& issue : resolver.issues) pack.issues.push_back(issue);
    std::sort(pack.programs.begin(), pack.programs.end(),
              [](const ShaderProgram& a, const ShaderProgram& b) { return a.name < b.name; });
    if (pack.programs.empty()) {
        if (error) *error = "shader pack contains no vertex or fragment programs";
    }
    return pack;
}

nlohmann::json PackTranslationResult::to_json() const {
    nlohmann::json j;
    j["pack"] = pack_name;
    j["format"] = format == PackFormat::OptiFine ? "optifine" :
                  format == PackFormat::Java ? "java" :
                  format == PackFormat::Mixed ? "mixed" : "unknown";
    j["summary"] = {{"full", full}, {"partial", partial}, {"unsupported", unsupported},
                    {"total", static_cast<int>(programs.size())}};
    j["programs"] = nlohmann::json::array();
    for (const auto& program : programs) {
        nlohmann::json p;
        p["name"] = program.name;
        p["stage"] = stage_name(program.stage);
        p["analysis"] = program.analysis.to_json();
        p["translation"] = program.translation.report;
        p["generated_source"] = program.translation.source;
        p["compile"] = {
            {"success", program.compile.success},
            {"validator_available", program.compile.validator_available},
            {"validator", program.compile.validator},
            {"log", program.compile.log}
        };
        j["programs"].push_back(std::move(p));
    }
    return j;
}

std::string PackTranslationResult::to_markdown() const {
    std::ostringstream out;
    out << "# Shader pack translation report\n\n";
    out << "- Pack: `" << pack_name << "`\n";
    out << "- Programs: " << programs.size() << "\n";
    out << "- FULL: " << full << ", PARTIAL: " << partial << ", UNSUPPORTED: " << unsupported << "\n\n";
    for (const auto& program : programs) {
        out << "## " << program.name << " (" << stage_name(program.stage) << ")\n\n";
        out << "- Compatibility: **" << compatibility_name(program.translation.level) << "**\n";
        out << "- Compile: " << (program.compile.success ? "passed" : "not passed") << "\n";
        if (!program.translation.uniform_map.empty()) {
            out << "- Uniform mappings: ";
            for (size_t i = 0; i < program.translation.uniform_map.size(); ++i) {
                if (i) out << ", ";
                out << program.translation.uniform_map[i].source << " -> "
                    << program.translation.uniform_map[i].target;
            }
            out << "\n";
        }
        for (const auto& issue : program.translation.issues) {
            if (issue.severity == IssueSeverity::Info) continue;
            out << "- " << severity_name(issue.severity) << " (`" << issue.code << "`): "
                << issue.message << "\n";
        }
        out << "\n";
    }
    return out.str();
}

PackTranslationResult analyze_and_translate_pack(const std::string& path,
                                                 const TranslationOptions& options,
                                                 bool compile) {
    PackTranslationResult result;
    std::string error;
    ShaderPack pack = load_shader_pack(path, {}, &error);
    result.pack_name = pack.name;
    result.format = pack.format;
    if (pack.programs.empty()) {
        ProgramTranslation failure;
        failure.name = "<pack>";
        failure.analysis.level = CompatibilityLevel::Unsupported;
        Issue issue;
        issue.severity = IssueSeverity::Error;
        issue.code = "pack_load_error";
        issue.message = error.empty() ? "no programs found" : error;
        failure.analysis.issues.push_back(issue);
        failure.translation.level = CompatibilityLevel::Unsupported;
        failure.translation.error = issue.message;
        failure.translation.issues.push_back(issue);
        result.programs.push_back(std::move(failure));
        result.unsupported = 1;
        return result;
    }

    for (const auto& program : pack.programs) {
        ProgramTranslation translated;
        translated.name = program.name;
        translated.stage = program.stage;
        translated.analysis = analyze_program_source(program.source, program.stage);
        translated.translation = translate_glsl(program.source, program.stage, options);
        // Preserve pack-level include errors in the program report.
        if (!program.error.empty()) {
            Issue issue;
            issue.severity = IssueSeverity::Warning;
            issue.code = "include_resolution";
            issue.message = program.error;
            translated.translation.issues.push_back(issue);
            if (translated.translation.level == CompatibilityLevel::Full) {
                translated.translation.level = CompatibilityLevel::Partial;
            }
        }
        if (compile) {
            CompileOptions copts;
            copts.stage = program.stage;
            translated.compile = compile_shader(translated.translation.source, copts);
        } else {
            translated.compile.success = true;
            translated.compile.log = "compile skipped";
        }
        switch (translated.translation.level) {
            case CompatibilityLevel::Full: ++result.full; break;
            case CompatibilityLevel::Partial: ++result.partial; break;
            case CompatibilityLevel::Unsupported: ++result.unsupported; break;
            case CompatibilityLevel::Unknown: ++result.unsupported; break;
        }
        result.programs.push_back(std::move(translated));
    }
    return result;
}

bool write_translation_output(const PackTranslationResult& result, const std::string& output_dir,
                              std::string& error) {
    if (!fs::ensure_directory(output_dir)) {
        error = "cannot create output directory: " + output_dir;
        return false;
    }
    for (const auto& program : result.programs) {
        const std::string base = sanitize_program_name(program.name);
        const std::string ext = program.stage == ShaderStage::Vertex ? ".vert" : ".frag";
        if (!program.translation.source.empty()) {
            if (!fs::write_file(fs::join(output_dir, base + ext), program.translation.source)) {
                error = "cannot write translated shader " + base + ext;
                return false;
            }
        }
        if (!fs::write_file(fs::join(output_dir, base + ".analysis.json"),
                            program.analysis.to_json().dump(2) + "\n")) {
            error = "cannot write analysis for " + program.name;
            return false;
        }
        if (!fs::write_file(fs::join(output_dir, base + ".translation.json"),
                            program.translation.report.dump(2) + "\n")) {
            error = "cannot write translation report for " + program.name;
            return false;
        }
    }
    if (!fs::write_file(fs::join(output_dir, "report.json"), result.to_json().dump(2) + "\n")) {
        error = "cannot write pack report";
        return false;
    }
    if (!fs::write_file(fs::join(output_dir, "REPORT.md"), result.to_markdown())) {
        error = "cannot write pack markdown report";
        return false;
    }
    return true;
}

} // namespace nexus::shader
