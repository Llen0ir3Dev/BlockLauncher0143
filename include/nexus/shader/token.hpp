#pragma once

#include <string>
#include <vector>

namespace nexus::shader {

enum class TokenType {
    End,
    Identifier,
    IntLiteral,
    FloatLiteral,
    Operator,
    Punctuation,
    Directive,
    Invalid
};

struct SourceLoc {
    int line = 1;
    int column = 1;
};

struct Token {
    TokenType type = TokenType::End;
    std::string text;
    SourceLoc loc;
    bool is(const char* value) const { return text == value; }
    bool is_identifier() const { return type == TokenType::Identifier; }
};

class Lexer {
public:
    std::vector<Token> tokenize(const std::string& source, std::string& error) const;
};

const char* token_type_name(TokenType type);

} // namespace nexus::shader
