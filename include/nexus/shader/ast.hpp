#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nexus/shader/token.hpp>

namespace nexus::shader {

enum class ShaderStage { Vertex, Fragment, Unknown };
const char* stage_name(ShaderStage stage);
bool parse_stage(const std::string& text, ShaderStage& out);

enum class CompatibilityLevel { Full, Partial, Unsupported, Unknown };
const char* compatibility_name(CompatibilityLevel level);

struct TypeSpec {
    std::string base;
    std::vector<std::string> qualifiers;
    std::vector<std::string> array_dims;
    SourceLoc loc;

    bool has_qualifier(const std::string& name) const;
    std::string qualifiers_string() const;
};

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    enum class Kind {
        IntLiteral,
        FloatLiteral,
        BoolLiteral,
        Identifier,
        Unary,
        Binary,
        Assign,
        Ternary,
        Call,
        Index,
        Field,
        Postfix,
        Paren,
        Comma
    };
    Kind kind = Kind::Identifier;
    std::string op;
    std::string value;
    std::vector<ExprPtr> args;
    SourceLoc loc;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct VarDecl;
using VarDeclPtr = std::unique_ptr<VarDecl>;

struct VarDecl {
    TypeSpec type;
    std::string name;
    ExprPtr init;
    SourceLoc loc;
};

struct Param {
    TypeSpec type;
    std::string name;
    SourceLoc loc;
};

struct Stmt {
    enum class Kind {
        Empty,
        Expr,
        Declaration,
        Block,
        If,
        For,
        While,
        DoWhile,
        Return,
        Break,
        Continue,
        Discard,
        Switch,
        Case,
        Default
    };
    Kind kind = Kind::Empty;
    ExprPtr expr;
    ExprPtr cond;
    ExprPtr init_expr;
    ExprPtr incr;
    std::vector<VarDeclPtr> decls;
    std::vector<StmtPtr> body;
    StmtPtr else_branch;
    SourceLoc loc;
};

struct FunctionDecl {
    TypeSpec return_type;
    std::string name;
    std::vector<Param> params;
    StmtPtr body;
    bool prototype = false;
    SourceLoc loc;
};

struct StructDecl {
    std::string name;
    std::vector<VarDecl> fields;
    SourceLoc loc;
};

struct PrecisionDecl {
    std::string precision;
    std::string type;
    SourceLoc loc;
};

struct TranslationUnit {
    int version = 110;
    bool es = false;
    ShaderStage stage = ShaderStage::Unknown;
    std::vector<std::string> directives;
    std::vector<std::string> extensions;
    std::vector<PrecisionDecl> precisions;
    std::vector<StructDecl> structs;
    std::vector<VarDecl> globals;
    std::vector<FunctionDecl> functions;
};

struct ParseOptions {
    ShaderStage stage = ShaderStage::Unknown;
    bool allow_unknown_declarations = true;
};

struct ParseResult {
    bool success = false;
    std::string error;
    TranslationUnit unit;
    std::vector<std::string> warnings;
    std::vector<Token> tokens;
};

ParseResult parse_glsl(const std::string& source, const ParseOptions& options = {});

std::string ast_dump(const TranslationUnit& unit);
std::string type_to_string(const TypeSpec& type);
std::string expr_to_string(const Expr& expr);

} // namespace nexus::shader
