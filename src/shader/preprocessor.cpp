#include <nexus/shader/preprocessor.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <sstream>

#include <nexus/core/strings.hpp>

namespace nexus::shader {

namespace {

struct CondFrame {
    bool parent_active = true;
    bool active = true;
    bool any_taken = false;
    bool saw_else = false;
};

std::string strip_directive(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i >= line.size() || line[i] != '#') return "";
    ++i;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    return line.substr(i);
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string expand_object_macros(const std::string& line, const std::map<std::string, std::string>& defines) {
    std::string current = line;
    for (int pass = 0; pass < 16; ++pass) {
        std::string out;
        bool changed = false;
        size_t i = 0;
        bool in_line_comment = false;
        while (i < current.size()) {
            if (!in_line_comment && current[i] == '/' && i + 1 < current.size() && current[i + 1] == '/') {
                in_line_comment = true;
            }
            if (in_line_comment) {
                out.push_back(current[i++]);
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(current[i])) || current[i] == '_') {
                const size_t start = i;
                while (i < current.size() && is_identifier_char(current[i])) ++i;
                const std::string word = current.substr(start, i - start);
                const auto it = defines.find(word);
                if (it != defines.end()) {
                    out += it->second;
                    changed = true;
                } else {
                    out += word;
                }
            } else {
                out.push_back(current[i++]);
            }
        }
        current = std::move(out);
        if (!changed) break;
    }
    return current;
}

class IfExprParser {
public:
    IfExprParser(const std::string& text, const std::map<std::string, std::string>& defines)
        : text_(text), defines_(defines) {}

    int64_t parse(bool& ok) {
        ok = true;
        pos_ = 0;
        const int64_t value = parse_or(ok);
        skip_ws();
        if (pos_ != text_.size()) ok = false;
        return value;
    }

private:
    void skip_ws() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    bool consume(const char* op) {
        skip_ws();
        const size_t len = std::strlen(op);
        if (text_.compare(pos_, len, op) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    std::string read_word() {
        skip_ws();
        const size_t start = pos_;
        while (pos_ < text_.size() && is_identifier_char(text_[pos_])) ++pos_;
        return text_.substr(start, pos_ - start);
    }

    int64_t parse_primary(bool& ok) {
        skip_ws();
        if (pos_ >= text_.size()) { ok = false; return 0; }
        if (consume("(")) {
            const int64_t v = parse_or(ok);
            if (!consume(")")) ok = false;
            return v;
        }
        if (consume("!")) return parse_primary(ok) ? 0 : 1;
        if (consume("~")) return ~parse_primary(ok);
        if (consume("-")) return -parse_primary(ok);
        if (consume("+")) return parse_primary(ok);
        if (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            const size_t start = pos_;
            while (pos_ < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == 'x' || text_[pos_] == 'X')) ++pos_;
            const std::string number = text_.substr(start, pos_ - start);
            try {
                return static_cast<int64_t>(std::stoll(number, nullptr, 0));
            } catch (...) {
                ok = false;
                return 0;
            }
        }
        const std::string word = read_word();
        if (word == "defined") {
            bool paren = consume("(");
            const std::string name = read_word();
            if (paren && !consume(")")) ok = false;
            return defines_.count(name) ? 1 : 0;
        }
        if (word == "true") return 1;
        if (word == "false") return 0;
        const auto it = defines_.find(word);
        if (it != defines_.end()) {
            try {
                return static_cast<int64_t>(std::stoll(it->second, nullptr, 0));
            } catch (...) {
                return 0;
            }
        }
        return 0;
    }

    int64_t parse_mul(bool& ok) {
        int64_t left = parse_primary(ok);
        while (ok) {
            if (consume("*")) left *= parse_primary(ok);
            else if (consume("/")) { const int64_t r = parse_primary(ok); if (r == 0) { ok = false; return 0; } left /= r; }
            else if (consume("%")) { const int64_t r = parse_primary(ok); if (r == 0) { ok = false; return 0; } left %= r; }
            else break;
        }
        return left;
    }

    int64_t parse_add(bool& ok) {
        int64_t left = parse_mul(ok);
        while (ok) {
            if (consume("+")) left += parse_mul(ok);
            else if (consume("-")) left -= parse_mul(ok);
            else break;
        }
        return left;
    }

    int64_t parse_shift(bool& ok) {
        int64_t left = parse_add(ok);
        while (ok) {
            if (consume("<<")) left <<= parse_add(ok);
            else if (consume(">>")) left >>= parse_add(ok);
            else break;
        }
        return left;
    }

    int64_t parse_rel(bool& ok) {
        int64_t left = parse_shift(ok);
        while (ok) {
            if (consume("<=")) left = left <= parse_shift(ok);
            else if (consume(">=")) left = left >= parse_shift(ok);
            else if (consume("<")) left = left < parse_shift(ok);
            else if (consume(">")) left = left > parse_shift(ok);
            else break;
        }
        return left;
    }

    int64_t parse_eq(bool& ok) {
        int64_t left = parse_rel(ok);
        while (ok) {
            if (consume("==")) left = left == parse_rel(ok);
            else if (consume("!=")) left = left != parse_rel(ok);
            else break;
        }
        return left;
    }

    int64_t parse_bitand(bool& ok) {
        int64_t left = parse_eq(ok);
        while (ok && consume("&")) left &= parse_eq(ok);
        return left;
    }

    int64_t parse_bitxor(bool& ok) {
        int64_t left = parse_bitand(ok);
        while (ok && consume("^")) left ^= parse_bitand(ok);
        return left;
    }

    int64_t parse_bitor(bool& ok) {
        int64_t left = parse_bitxor(ok);
        while (ok && consume("|")) left |= parse_bitxor(ok);
        return left;
    }

    int64_t parse_and(bool& ok) {
        int64_t left = parse_bitor(ok);
        while (ok && consume("&&")) {
            const int64_t right = parse_bitor(ok);
            left = (left != 0 && right != 0) ? 1 : 0;
        }
        return left;
    }

    int64_t parse_or(bool& ok) {
        int64_t left = parse_and(ok);
        while (ok && consume("||")) {
            const int64_t right = parse_and(ok);
            left = (left != 0 || right != 0) ? 1 : 0;
        }
        return left;
    }

    std::string text_;
    const std::map<std::string, std::string>& defines_;
    size_t pos_ = 0;
};

} // namespace

PreprocessResult preprocess_glsl(const std::string& source, const PreprocessOptions& options) {
    PreprocessResult result;
    result.version = 110;
    result.es = false;
    std::map<std::string, std::string> defines = options.defines;
    if (options.stage == ShaderStage::Vertex) defines["GL_VERTEX_SHADER"] = "1";
    if (options.stage == ShaderStage::Fragment) defines["GL_FRAGMENT_SHADER"] = "1";
    defines["GL_ES"] = "1";

    std::vector<CondFrame> stack;
    auto active = [&]() {
        for (const auto& frame : stack) if (!frame.active) return false;
        return true;
    };

    std::istringstream in(source);
    std::string line;
    std::ostringstream out;
    int line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string directive = strip_directive(line);
        if (directive.empty() && (line.empty() || line[0] != '#')) {
            if (active()) {
                std::string expanded = expand_object_macros(line, defines);
                out << expanded << "\n";
            } else {
                out << "\n";
            }
            continue;
        }

        std::istringstream ds(directive);
        std::string keyword;
        ds >> keyword;
        std::string rest;
        std::getline(ds, rest);
        rest = trim(rest);

        if (keyword == "version") {
            std::istringstream vs(rest);
            int version = 0;
            std::string profile;
            vs >> version >> profile;
            if (version > 0) result.version = version;
            result.es = (profile == "es") || version == 100 || version == 300;
            result.directives.push_back("version " + rest);
        } else if (keyword == "extension") {
            result.extensions.push_back(rest);
            result.directives.push_back("extension " + rest);
        } else if (keyword == "define") {
            if (active()) {
                const size_t name_end = rest.find_first_of(" \t(");
                const std::string name = rest.substr(0, name_end);
                if (name.empty()) {
                    result.warnings.push_back("empty #define at line " + std::to_string(line_number));
                } else if (name_end != std::string::npos && rest[name_end] == '(') {
                    result.warnings.push_back("function-like macro not expanded: " + name);
                } else {
                    std::string value =
                        name_end == std::string::npos ? "" : trim(rest.substr(name_end));
                    defines[name] = value;
                }
            }
            result.directives.push_back("define " + rest);
            out << "\n";
        } else if (keyword == "undef") {
            if (active()) defines.erase(trim(rest));
            result.directives.push_back("undef " + rest);
            out << "\n";
        } else if (keyword == "ifdef" || keyword == "ifndef" || keyword == "if") {
            CondFrame frame;
            frame.parent_active = active();
            bool condition = false;
            if (keyword == "ifdef") {
                condition = defines.count(trim(rest)) > 0;
            } else if (keyword == "ifndef") {
                condition = defines.count(trim(rest)) == 0;
            } else {
                bool ok = false;
                IfExprParser parser(expand_object_macros(rest, defines), defines);
                condition = parser.parse(ok) != 0;
                if (!ok) result.warnings.push_back("could not fully evaluate #if at line " + std::to_string(line_number));
            }
            frame.active = frame.parent_active && condition;
            frame.any_taken = frame.active;
            stack.push_back(frame);
            out << "\n";
        } else if (keyword == "elif") {
            if (stack.empty()) {
                result.error = "#elif without #if at line " + std::to_string(line_number);
                return result;
            }
            CondFrame& frame = stack.back();
            if (frame.saw_else) {
                result.error = "#elif after #else at line " + std::to_string(line_number);
                return result;
            }
            bool condition = false;
            bool ok = false;
            IfExprParser parser(expand_object_macros(rest, defines), defines);
            condition = parser.parse(ok) != 0;
            if (!ok) result.warnings.push_back("could not fully evaluate #elif at line " + std::to_string(line_number));
            frame.active = frame.parent_active && !frame.any_taken && condition;
            frame.any_taken = frame.any_taken || frame.active;
            out << "\n";
        } else if (keyword == "else") {
            if (stack.empty()) {
                result.error = "#else without #if at line " + std::to_string(line_number);
                return result;
            }
            CondFrame& frame = stack.back();
            if (frame.saw_else) {
                result.error = "duplicate #else at line " + std::to_string(line_number);
                return result;
            }
            frame.saw_else = true;
            frame.active = frame.parent_active && !frame.any_taken;
            frame.any_taken = frame.any_taken || frame.active;
            out << "\n";
        } else if (keyword == "endif") {
            if (stack.empty()) {
                result.error = "#endif without #if at line " + std::to_string(line_number);
                return result;
            }
            stack.pop_back();
            out << "\n";
        } else if (keyword == "include") {
            result.warnings.push_back("#include was not resolved before preprocessing at line " + std::to_string(line_number));
            out << "\n";
        } else if (keyword == "error") {
            if (active()) {
                result.error = "#error: " + rest;
                return result;
            }
            out << "\n";
        } else if (keyword == "pragma" || keyword == "line") {
            if (!rest.empty()) result.directives.push_back(keyword + " " + rest);
            out << "\n";
        } else if (keyword.empty()) {
            out << "\n";
        } else {
            result.warnings.push_back("unknown preprocessor directive #" + keyword + " at line " + std::to_string(line_number));
            out << "\n";
        }
    }
    if (!stack.empty()) {
        result.error = "unterminated preprocessor conditional";
        return result;
    }
    result.source = out.str();
    result.success = true;
    return result;
}

} // namespace nexus::shader
