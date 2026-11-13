#include <nexus/shader/token.hpp>

#include <cctype>
#include <cstring>
#include <sstream>

namespace nexus::shader {

const char* token_type_name(TokenType type) {
    switch (type) {
        case TokenType::End: return "End";
        case TokenType::Identifier: return "Identifier";
        case TokenType::IntLiteral: return "IntLiteral";
        case TokenType::FloatLiteral: return "FloatLiteral";
        case TokenType::Operator: return "Operator";
        case TokenType::Punctuation: return "Punctuation";
        case TokenType::Directive: return "Directive";
        case TokenType::Invalid: return "Invalid";
    }
    return "?";
}

std::vector<Token> Lexer::tokenize(const std::string& source, std::string& error) const {
    std::vector<Token> tokens;
    size_t i = 0;
    int line = 1;
    int column = 1;
    auto advance = [&](size_t n = 1) {
        for (size_t k = 0; k < n; ++k) {
            if (i < source.size() && source[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
            ++i;
        }
    };
    auto add = [&](TokenType type, const std::string& text, int token_line, int token_column) {
        Token t;
        t.type = type;
        t.text = text;
        t.loc.line = token_line;
        t.loc.column = token_column;
        tokens.push_back(std::move(t));
    };

    while (i < source.size()) {
        const char c = source[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
            advance();
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') advance();
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            const int start_line = line;
            advance(2);
            bool closed = false;
            while (i + 1 < source.size()) {
                if (source[i] == '*' && source[i + 1] == '/') {
                    advance(2);
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                std::ostringstream ss;
                ss << "unterminated block comment starting at line " << start_line;
                error = ss.str();
                return tokens;
            }
            continue;
        }
        if (c == '#') {
            const int token_line = line;
            const int token_column = column;
            advance();
            std::string text;
            while (i < source.size() && source[i] != '\n') {
                text.push_back(source[i]);
                advance();
            }
            add(TokenType::Directive, text, token_line, token_column);
            continue;
        }
        const int token_line = line;
        const int token_column = column;
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string text;
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) {
                text.push_back(source[i]);
                advance();
            }
            add(TokenType::Identifier, text, token_line, token_column);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1])))) {
            std::string text;
            bool is_float = false;
            if (c == '0' && i + 1 < source.size() && (source[i + 1] == 'x' || source[i + 1] == 'X')) {
                text.push_back(source[i]); advance();
                text.push_back(source[i]); advance();
                while (i < source.size() && std::isxdigit(static_cast<unsigned char>(source[i]))) {
                    text.push_back(source[i]); advance();
                }
            } else {
                while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
                    text.push_back(source[i]); advance();
                }
                if (i < source.size() && source[i] == '.') {
                    is_float = true;
                    text.push_back(source[i]); advance();
                    while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
                        text.push_back(source[i]); advance();
                    }
                }
                if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                    is_float = true;
                    text.push_back(source[i]); advance();
                    if (i < source.size() && (source[i] == '+' || source[i] == '-')) {
                        text.push_back(source[i]); advance();
                    }
                    while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
                        text.push_back(source[i]); advance();
                    }
                }
            }
            if (i < source.size() && (source[i] == 'f' || source[i] == 'F')) {
                is_float = true;
                text.push_back(source[i]); advance();
            } else if (i < source.size() && (source[i] == 'u' || source[i] == 'U')) {
                text.push_back(source[i]); advance();
            }
            add(is_float ? TokenType::FloatLiteral : TokenType::IntLiteral, text, token_line, token_column);
            continue;
        }
        static const char* multi_ops[] = {
            "<<=", ">>=", "++", "--", "&&", "||", "^^", "<=", ">=", "==", "!=",
            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "::"
        };
        bool matched = false;
        for (const char* op : multi_ops) {
            const size_t len = std::strlen(op);
            if (source.compare(i, len, op) == 0) {
                add(TokenType::Operator, op, token_line, token_column);
                advance(len);
                matched = true;
                break;
            }
        }
        if (matched) continue;
        const std::string single = std::string(1, c);
        if (std::strchr("+-*/%<>=!&|^~?:", c)) {
            add(TokenType::Operator, single, token_line, token_column);
            advance();
            continue;
        }
        if (std::strchr("()[]{},;.", c)) {
            add(TokenType::Punctuation, single, token_line, token_column);
            advance();
            continue;
        }
        std::ostringstream ss;
        ss << "unexpected character '" << c << "' at line " << line << ", column " << column;
        error = ss.str();
        return tokens;
    }
    add(TokenType::End, "", line, column);
    return tokens;
}

} // namespace nexus::shader
