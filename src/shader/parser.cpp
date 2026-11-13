#include <nexus/shader/ast.hpp>

#include <cctype>
#include <sstream>
#include <unordered_set>

#include <nexus/core/strings.hpp>
#include <nexus/shader/lexer.hpp>
#include <nexus/shader/token.hpp>

namespace nexus::shader {

const char* stage_name(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return "vertex";
        case ShaderStage::Fragment: return "fragment";
        case ShaderStage::Unknown: return "unknown";
    }
    return "unknown";
}

bool parse_stage(const std::string& text, ShaderStage& out) {
    const std::string lower = to_lower(text);
    if (lower == "vert" || lower == "vertex" || lower == "vsh" || lower == "vs") { out = ShaderStage::Vertex; return true; }
    if (lower == "frag" || lower == "fragment" || lower == "fsh" || lower == "fs" || lower == "pixel") {
        out = ShaderStage::Fragment;
        return true;
    }
    return false;
}

const char* compatibility_name(CompatibilityLevel level) {
    switch (level) {
        case CompatibilityLevel::Full: return "FULL";
        case CompatibilityLevel::Partial: return "PARTIAL";
        case CompatibilityLevel::Unsupported: return "UNSUPPORTED";
        case CompatibilityLevel::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

bool TypeSpec::has_qualifier(const std::string& name) const {
    for (const auto& q : qualifiers) {
        if (q == name) return true;
    }
    return false;
}

std::string TypeSpec::qualifiers_string() const {
    return join(qualifiers, " ");
}

namespace {

const std::unordered_set<std::string>& qualifier_set() {
    static const std::unordered_set<std::string> set = {
        "const", "uniform", "attribute", "varying", "in", "out", "inout",
        "centroid", "flat", "smooth", "noperspective", "highp", "mediump",
        "lowp", "invariant", "precise", "readonly", "writeonly", "coherent",
        "volatile", "restrict", "shared", "buffer", "patch", "sample"
    };
    return set;
}

const std::unordered_set<std::string>& type_set() {
    static const std::unordered_set<std::string> set = {
        "void", "bool", "int", "uint", "float", "double",
        "vec2", "vec3", "vec4", "bvec2", "bvec3", "bvec4",
        "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4",
        "dvec2", "dvec3", "dvec4",
        "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4",
        "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4",
        "sampler1D", "sampler2D", "sampler3D", "samplerCube",
        "sampler2DShadow", "samplerCubeShadow", "sampler2DArray",
        "sampler2DArrayShadow", "sampler2DMS", "isampler2D", "usampler2D",
        "samplerExternalOES", "image2D", "atomic_uint"
    };
    return set;
}

bool is_qualifier(const std::string& text) { return qualifier_set().count(text) > 0; }
bool is_type_keyword(const std::string& text) { return type_set().count(text) > 0; }

class Parser {
public:
    Parser(std::vector<Token> tokens, const ParseOptions& options)
        : tokens_(std::move(tokens)), options_(options) {}

    ParseResult run() {
        result_.tokens = tokens_;
        result_.unit.stage = options_.stage;
        while (error_.empty() && !at_end()) {
            if (peek().type == TokenType::Directive) {
                handle_directive(advance().text);
                continue;
            }
            if (peek().is("precision")) {
                parse_precision();
                continue;
            }
            if (peek().is("struct")) {
                parse_struct();
                continue;
            }
            if (peek().is(";")) {
                advance();
                continue;
            }
            if (!parse_external_declaration()) {
                if (error_.empty()) error_ = "unexpected token '" + peek().text + "'";
                break;
            }
        }
        result_.unit.version = version_;
        result_.unit.es = es_;
        result_.success = error_.empty();
        result_.error = error_;
        result_.warnings = warnings_;
        if (!error_.empty()) {
            std::ostringstream ss;
            ss << error_ << " at line " << error_loc_.line << ", column " << error_loc_.column;
            result_.error = ss.str();
        }
        return std::move(result_);
    }

private:
    const Token& peek(size_t offset = 0) const {
        const size_t index = std::min(index_ + offset, tokens_.size() - 1);
        return tokens_[index];
    }
    const Token& advance() {
        const Token& token = tokens_[index_];
        if (index_ + 1 < tokens_.size()) ++index_;
        return token;
    }
    bool at_end() const { return peek().type == TokenType::End; }
    bool match(const char* text) {
        if (peek().text == text) { advance(); return true; }
        return false;
    }
    bool expect(const char* text) {
        if (match(text)) return true;
        fail("expected '" + std::string(text) + "', got '" + peek().text + "'");
        return false;
    }
    void fail(const std::string& message) {
        if (error_.empty()) {
            error_ = message;
            error_loc_ = peek().loc;
        }
    }
    std::string expect_identifier() {
        if (peek().type == TokenType::Identifier) return advance().text;
        fail("expected identifier, got '" + peek().text + "'");
        return "";
    }

    void handle_directive(const std::string& text) {
        const std::string directive = trim(text);
        result_.unit.directives.push_back(directive);
        std::istringstream ss(directive);
        std::string keyword;
        ss >> keyword;
        if (keyword == "version") {
            int version = 0;
            std::string profile;
            ss >> version >> profile;
            if (version > 0) version_ = version;
            es_ = profile == "es" || version == 100 || version == 300;
        } else if (keyword == "extension") {
            std::string rest;
            std::getline(ss, rest);
            result_.unit.extensions.push_back(trim(rest));
        }
    }

    void parse_precision() {
        const Token start = advance();
        PrecisionDecl decl;
        decl.loc = start.loc;
        decl.precision = expect_identifier();
        decl.type = expect_identifier();
        expect(";");
        result_.unit.precisions.push_back(decl);
    }

    bool parse_struct() {
        const Token start = advance();
        StructDecl decl;
        decl.loc = start.loc;
        decl.name = expect_identifier();
        if (!expect("{")) return false;
        while (error_.empty() && !peek().is("}") && !at_end()) {
            TypeSpec type = parse_type_spec();
            if (!error_.empty()) return false;
            std::string name = expect_identifier();
            std::string dims = parse_array_suffix();
            VarDecl field;
            field.type = type;
            field.name = name;
            field.loc = peek().loc;
            decl.fields.push_back(std::move(field));
            if (!match(";")) {
                fail("expected ';' after struct field");
                return false;
            }
        }
        if (!expect("}")) return false;
        const std::string struct_name = decl.name;
        result_.unit.structs.push_back(std::move(decl));
        // Optional instance declarators after the struct body.
        if (!peek().is(";") && peek().type == TokenType::Identifier) {
            std::vector<VarDeclPtr> vars;
            if (!parse_declarator_list(struct_name, vars, true)) return false;
        } else {
            expect(";");
        }
        return true;
    }

    TypeSpec parse_type_spec() {
        TypeSpec type;
        type.loc = peek().loc;
        while (peek().type == TokenType::Identifier && is_qualifier(peek().text)) {
            type.qualifiers.push_back(advance().text);
        }
        if (peek().is("layout")) {
            advance();
            if (match("(")) {
                int depth = 1;
                while (!at_end() && depth > 0) {
                    if (peek().is("(")) ++depth;
                    else if (peek().is(")")) --depth;
                    advance();
                }
            }
            type.qualifiers.push_back("layout");
        }
        if (peek().is("struct")) {
            // Inline struct type: `struct Name { ... }`.  Parsed as a named struct
            // first, then used as the base type.
            if (!parse_struct()) return type;
            type.base = result_.unit.structs.back().name;
        } else if (peek().type == TokenType::Identifier) {
            type.base = advance().text;
        } else {
            fail("expected type name, got '" + peek().text + "'");
        }
        type.array_dims.push_back(parse_array_suffix());
        while (!type.array_dims.back().empty()) {
            type.array_dims.push_back(parse_array_suffix());
        }
        if (!type.array_dims.empty() && type.array_dims.back().empty()) type.array_dims.pop_back();
        return type;
    }

    std::string parse_array_suffix() {
        if (!peek().is("[")) return "";
        advance();
        std::string dim;
        int depth = 0;
        while (!at_end()) {
            if (peek().is("[") || peek().is("(")) ++depth;
            if (peek().is("]")) {
                if (depth == 0) { advance(); break; }
                --depth;
            } else if (peek().is(")")) {
                --depth;
            }
            dim += advance().text;
        }
        return dim;
    }

    bool looks_like_declaration() const {
        if (peek().type != TokenType::Identifier) return false;
        if (is_qualifier(peek().text) || is_type_keyword(peek().text)) return true;
        if (peek(1).type == TokenType::Identifier) return true;
        if (peek(1).is("[")) return true;
        return false;
    }

    bool parse_external_declaration() {
        const Token start = peek();
        TypeSpec type = parse_type_spec();
        if (!error_.empty()) return false;
        if (peek().is("{")) {
            // Interface block: `uniform Block { ... } instance;`
            warnings_.push_back("interface block at line " + std::to_string(start.loc.line) + " is not supported by GLSL ES 1.00");
            int depth = 0;
            do {
                if (peek().is("{")) ++depth;
                else if (peek().is("}")) --depth;
                advance();
            } while (!at_end() && depth > 0);
            while (!at_end() && !peek().is(";")) advance();
            match(";");
            return true;
        }
        if (peek().is(";")) {
            advance();
            return true;
        }
        const std::string name = expect_identifier();
        if (name.empty()) return false;
        if (peek().is("(")) {
            return parse_function(type, name, start.loc);
        }
        std::vector<VarDeclPtr> vars;
        if (!parse_declarator_tail(type, name, vars)) return false;
        if (!expect(";")) return false;
        for (auto& var : vars) result_.unit.globals.push_back(std::move(*var));
        return true;
    }

    bool parse_declarator_tail(const TypeSpec& type, const std::string& name,
                               std::vector<VarDeclPtr>& vars) {
        std::string current = name;
        while (true) {
            auto var = std::make_unique<VarDecl>();
            var->type = type;
            var->name = current;
            var->loc = peek().loc;
            const std::string dim = parse_array_suffix();
            if (!dim.empty()) var->type.array_dims.push_back(dim);
            if (match("=")) {
                var->init = parse_assignment();
                if (!error_.empty()) return false;
            }
            vars.push_back(std::move(var));
            if (!match(",")) break;
            current = expect_identifier();
            if (current.empty()) return false;
        }
        return true;
    }

    bool parse_declarator_list(const std::string& base, std::vector<VarDeclPtr>& vars, bool semicolon) {
        TypeSpec parsed;
        parsed.base = base;
        std::string name = expect_identifier();
        if (name.empty()) return false;
        if (!parse_declarator_tail(parsed, name, vars)) return false;
        if (semicolon) {
            if (!expect(";")) return false;
            for (auto& var : vars) result_.unit.globals.push_back(std::move(*var));
            vars.clear();
        }
        return true;
    }

    bool parse_function(const TypeSpec& return_type, const std::string& name, const SourceLoc& loc) {
        if (!expect("(")) return false;
        FunctionDecl fn;
        fn.return_type = return_type;
        fn.name = name;
        fn.loc = loc;
        if (peek().is("void") && peek(1).is(")")) {
            advance();
        } else if (!peek().is(")")) {
            while (error_.empty() && !at_end()) {
                Param param;
                param.loc = peek().loc;
                param.type = parse_type_spec();
                if (!error_.empty()) return false;
                if (peek().type == TokenType::Identifier) param.name = advance().text;
                const std::string dim = parse_array_suffix();
                if (!dim.empty()) param.type.array_dims.push_back(dim);
                fn.params.push_back(std::move(param));
                if (!match(",")) break;
            }
        }
        if (!expect(")")) return false;
        if (match(";")) {
            fn.prototype = true;
            result_.unit.functions.push_back(std::move(fn));
            return true;
        }
        if (!peek().is("{")) {
            fail("expected function body or ';'");
            return false;
        }
        fn.body = parse_block();
        if (!error_.empty()) return false;
        result_.unit.functions.push_back(std::move(fn));
        return true;
    }

    StmtPtr parse_block() {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Block;
        stmt->loc = peek().loc;
        if (!expect("{")) return stmt;
        while (error_.empty() && !peek().is("}") && !at_end()) {
            auto child = parse_statement();
            if (child) stmt->body.push_back(std::move(child));
        }
        expect("}");
        return stmt;
    }

    StmtPtr parse_statement() {
        const Token start = peek();
        auto stmt = std::make_unique<Stmt>();
        stmt->loc = start.loc;
        if (start.is("{")) return parse_block();
        if (start.is(";")) { advance(); stmt->kind = Stmt::Kind::Empty; return stmt; }
        if (start.is("if")) {
            advance();
            stmt->kind = Stmt::Kind::If;
            expect("(");
            stmt->cond = parse_expression();
            expect(")");
            auto then_branch = parse_statement();
            stmt->body.push_back(std::move(then_branch));
            if (match("else")) stmt->else_branch = parse_statement();
            return stmt;
        }
        if (start.is("for")) {
            advance();
            stmt->kind = Stmt::Kind::For;
            expect("(");
            if (!peek().is(";")) {
                if (looks_like_declaration()) {
                    std::vector<VarDeclPtr> vars;
                    TypeSpec type = parse_type_spec();
                    std::string name = expect_identifier();
                    if (!parse_declarator_tail_without_semicolon(type, name, vars)) return stmt;
                    stmt->decls = std::move(vars);
                } else {
                    stmt->init_expr = parse_expression();
                }
            }
            expect(";");
            if (!peek().is(";")) stmt->cond = parse_expression();
            expect(";");
            if (!peek().is(")")) stmt->incr = parse_expression();
            expect(")");
            auto body = parse_statement();
            stmt->body.push_back(std::move(body));
            return stmt;
        }
        if (start.is("while")) {
            advance();
            stmt->kind = Stmt::Kind::While;
            expect("(");
            stmt->cond = parse_expression();
            expect(")");
            stmt->body.push_back(parse_statement());
            return stmt;
        }
        if (start.is("do")) {
            advance();
            stmt->kind = Stmt::Kind::DoWhile;
            stmt->body.push_back(parse_statement());
            expect("while");
            expect("(");
            stmt->cond = parse_expression();
            expect(")");
            expect(";");
            return stmt;
        }
        if (start.is("return")) {
            advance();
            stmt->kind = Stmt::Kind::Return;
            if (!peek().is(";")) stmt->expr = parse_expression();
            expect(";");
            return stmt;
        }
        if (start.is("break")) { advance(); stmt->kind = Stmt::Kind::Break; expect(";"); return stmt; }
        if (start.is("continue")) { advance(); stmt->kind = Stmt::Kind::Continue; expect(";"); return stmt; }
        if (start.is("discard")) { advance(); stmt->kind = Stmt::Kind::Discard; expect(";"); return stmt; }
        if (start.is("switch")) {
            advance();
            stmt->kind = Stmt::Kind::Switch;
            expect("(");
            stmt->cond = parse_expression();
            expect(")");
            stmt->body.push_back(parse_block());
            return stmt;
        }
        if (start.is("case")) {
            advance();
            stmt->kind = Stmt::Kind::Case;
            stmt->expr = parse_expression();
            expect(":");
            return stmt;
        }
        if (start.is("default") && peek(1).is(":")) {
            advance();
            advance();
            stmt->kind = Stmt::Kind::Default;
            return stmt;
        }
        if (looks_like_declaration()) {
            stmt->kind = Stmt::Kind::Declaration;
            TypeSpec type = parse_type_spec();
            if (!error_.empty()) return stmt;
            if (peek().is(";")) {
                advance();
                return stmt;
            }
            std::string name = expect_identifier();
            if (name.empty()) return stmt;
            std::vector<VarDeclPtr> vars;
            if (!parse_declarator_tail(type, name, vars)) return stmt;
            if (!expect(";")) return stmt;
            stmt->decls = std::move(vars);
            return stmt;
        }
        stmt->kind = Stmt::Kind::Expr;
        stmt->expr = parse_expression();
        expect(";");
        return stmt;
    }

    bool parse_declarator_tail_without_semicolon(const TypeSpec& type, const std::string& name,
                                                  std::vector<VarDeclPtr>& vars) {
        auto var = std::make_unique<VarDecl>();
        var->type = type;
        var->name = name;
        var->loc = peek().loc;
        const std::string dim = parse_array_suffix();
        if (!dim.empty()) var->type.array_dims.push_back(dim);
        if (match("=")) var->init = parse_assignment();
        vars.push_back(std::move(var));
        while (match(",")) {
            const std::string next = expect_identifier();
            if (next.empty()) return false;
            if (!parse_declarator_tail_without_semicolon(type, next, vars)) return false;
        }
        return true;
    }

    ExprPtr make_binary(const std::string& op, ExprPtr left, ExprPtr right, const SourceLoc& loc) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Binary;
        expr->op = op;
        expr->loc = loc;
        expr->args.push_back(std::move(left));
        expr->args.push_back(std::move(right));
        return expr;
    }

    ExprPtr parse_expression() {
        auto left = parse_assignment();
        while (match(",")) {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Comma;
            expr->loc = peek().loc;
            expr->args.push_back(std::move(left));
            expr->args.push_back(parse_assignment());
            left = std::move(expr);
        }
        return left;
    }

    ExprPtr parse_assignment() {
        auto left = parse_ternary();
        static const std::vector<std::string> ops = {
            "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
        };
        for (const auto& op : ops) {
            if (peek().is(op.c_str())) {
                const SourceLoc loc = peek().loc;
                advance();
                auto expr = std::make_unique<Expr>();
                expr->kind = Expr::Kind::Assign;
                expr->op = op;
                expr->loc = loc;
                expr->args.push_back(std::move(left));
                expr->args.push_back(parse_assignment());
                return expr;
            }
        }
        return left;
    }

    ExprPtr parse_ternary() {
        auto cond = parse_binary(0);
        if (match("?")) {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Ternary;
            expr->loc = peek().loc;
            expr->args.push_back(std::move(cond));
            expr->args.push_back(parse_expression());
            expect(":");
            expr->args.push_back(parse_assignment());
            return expr;
        }
        return cond;
    }

    ExprPtr parse_binary(int min_precedence) {
        auto left = parse_unary();
        static const std::vector<std::pair<std::string, int>> precedence = {
            {"||", 1}, {"^^", 2}, {"&&", 3}, {"|", 4}, {"^", 5}, {"&", 6},
            {"==", 7}, {"!=", 7}, {"<", 8}, {">", 8}, {"<=", 8}, {">=", 8},
            {"<<", 9}, {">>", 9}, {"+", 10}, {"-", 10}, {"*", 11}, {"/", 11}, {"%", 11}
        };
        while (error_.empty() && !at_end()) {
            int best = -1;
            std::string op;
            for (const auto& item : precedence) {
                if (peek().is(item.first.c_str())) {
                    if (item.second >= min_precedence) {
                        best = item.second;
                        op = item.first;
                        break;
                    }
                }
            }
            if (best < 0) break;
            const SourceLoc loc = peek().loc;
            advance();
            auto right = parse_binary(best + 1);
            left = make_binary(op, std::move(left), std::move(right), loc);
        }
        return left;
    }

    ExprPtr parse_unary() {
        const Token token = peek();
        if (token.type == TokenType::Operator) {
            if (token.text == "+" || token.text == "-" || token.text == "!" ||
                token.text == "~" || token.text == "++" || token.text == "--") {
                advance();
                auto expr = std::make_unique<Expr>();
                expr->kind = Expr::Kind::Unary;
                expr->op = token.text;
                expr->loc = token.loc;
                expr->args.push_back(parse_unary());
                return expr;
            }
        }
        return parse_postfix();
    }

    ExprPtr parse_postfix() {
        auto expr = parse_primary();
        while (error_.empty() && !at_end()) {
            if (match("(")) {
                auto call = std::make_unique<Expr>();
                call->kind = Expr::Kind::Call;
                call->loc = expr->loc;
                if (expr->kind == Expr::Kind::Identifier) call->value = expr->value;
                else call->args.push_back(std::move(expr));
                if (!peek().is(")")) {
                    do {
                        call->args.push_back(parse_assignment());
                    } while (match(","));
                }
                expect(")");
                expr = std::move(call);
            } else if (match("[")) {
                auto index = std::make_unique<Expr>();
                index->kind = Expr::Kind::Index;
                index->loc = expr->loc;
                index->args.push_back(std::move(expr));
                index->args.push_back(parse_expression());
                expect("]");
                expr = std::move(index);
            } else if (match(".")) {
                auto field = std::make_unique<Expr>();
                field->kind = Expr::Kind::Field;
                field->loc = expr->loc;
                field->value = expect_identifier();
                field->args.push_back(std::move(expr));
                expr = std::move(field);
            } else if (peek().is("++") || peek().is("--")) {
                auto post = std::make_unique<Expr>();
                post->kind = Expr::Kind::Postfix;
                post->op = advance().text;
                post->loc = expr->loc;
                post->args.push_back(std::move(expr));
                expr = std::move(post);
            } else {
                break;
            }
        }
        return expr;
    }

    ExprPtr parse_primary() {
        const Token token = peek();
        if (token.type == TokenType::IntLiteral) {
            advance();
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::IntLiteral;
            expr->value = token.text;
            expr->loc = token.loc;
            return expr;
        }
        if (token.type == TokenType::FloatLiteral) {
            advance();
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::FloatLiteral;
            expr->value = token.text;
            expr->loc = token.loc;
            return expr;
        }
        if (token.type == TokenType::Identifier) {
            advance();
            auto expr = std::make_unique<Expr>();
            if (token.text == "true" || token.text == "false") {
                expr->kind = Expr::Kind::BoolLiteral;
            } else {
                expr->kind = Expr::Kind::Identifier;
            }
            expr->value = token.text;
            expr->loc = token.loc;
            // Array constructor syntax: float[3](...)
            if (peek().is("[") && is_type_keyword(token.text)) {
                while (match("[")) {
                    parse_expression();
                    expect("]");
                }
            }
            return expr;
        }
        if (match("(")) {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Paren;
            expr->loc = token.loc;
            expr->args.push_back(parse_expression());
            expect(")");
            return expr;
        }
        fail("unexpected token in expression: '" + token.text + "'");
        advance();
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Identifier;
        expr->value = "<error>";
        expr->loc = token.loc;
        return expr;
    }

    std::vector<Token> tokens_;
    ParseOptions options_;
    size_t index_ = 0;
    std::string error_;
    SourceLoc error_loc_;
    std::vector<std::string> warnings_;
    int version_ = 110;
    bool es_ = false;
    ParseResult result_;
};

} // namespace

ParseResult parse_glsl(const std::string& source, const ParseOptions& options) {
    Lexer lexer;
    std::string lex_error;
    auto tokens = lexer.tokenize(source, lex_error);
    if (!lex_error.empty()) {
        ParseResult result;
        result.error = lex_error;
        return result;
    }
    Parser parser(std::move(tokens), options);
    return parser.run();
}

std::string type_to_string(const TypeSpec& type) {
    std::string out;
    if (!type.qualifiers.empty()) out += join(type.qualifiers, " ") + " ";
    out += type.base;
    for (const auto& dim : type.array_dims) {
        if (!dim.empty()) out += "[" + dim + "]";
    }
    return out;
}

std::string expr_to_string(const Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::IntLiteral:
        case Expr::Kind::FloatLiteral:
        case Expr::Kind::BoolLiteral:
        case Expr::Kind::Identifier:
            return expr.value;
        case Expr::Kind::Unary:
            return expr.op + expr_to_string(*expr.args[0]);
        case Expr::Kind::Postfix:
            return expr_to_string(*expr.args[0]) + expr.op;
        case Expr::Kind::Binary:
            return "(" + expr_to_string(*expr.args[0]) + " " + expr.op + " " + expr_to_string(*expr.args[1]) + ")";
        case Expr::Kind::Assign:
            return expr_to_string(*expr.args[0]) + " " + expr.op + " " + expr_to_string(*expr.args[1]);
        case Expr::Kind::Ternary:
            return expr_to_string(*expr.args[0]) + " ? " + expr_to_string(*expr.args[1]) + " : " + expr_to_string(*expr.args[2]);
        case Expr::Kind::Call: {
            std::string out = expr.value.empty() ? "" : expr.value;
            if (!expr.args.empty() && expr.value.empty()) {
                out += expr_to_string(*expr.args[0]);
            }
            out += "(";
            size_t start = expr.value.empty() ? 1 : 0;
            for (size_t i = start; i < expr.args.size(); ++i) {
                if (i > start) out += ", ";
                out += expr_to_string(*expr.args[i]);
            }
            out += ")";
            return out;
        }
        case Expr::Kind::Index:
            return expr_to_string(*expr.args[0]) + "[" + expr_to_string(*expr.args[1]) + "]";
        case Expr::Kind::Field:
            return expr_to_string(*expr.args[0]) + "." + expr.value;
        case Expr::Kind::Paren:
            return "(" + expr_to_string(*expr.args[0]) + ")";
        case Expr::Kind::Comma: {
            std::string out;
            for (size_t i = 0; i < expr.args.size(); ++i) {
                if (i) out += ", ";
                out += expr_to_string(*expr.args[i]);
            }
            return out;
        }
    }
    return "";
}

static void dump_statement(const Stmt& stmt, std::ostringstream& out, int indent);

static void dump_var(const VarDecl& var, std::ostringstream& out, int indent) {
    out << std::string(indent, ' ') << type_to_string(var.type) << " " << var.name;
    if (var.init) out << " = " << expr_to_string(*var.init);
    out << "\n";
}

static void dump_statement(const Stmt& stmt, std::ostringstream& out, int indent) {
    const std::string pad(indent, ' ');
    switch (stmt.kind) {
        case Stmt::Kind::Empty: out << pad << ";\n"; break;
        case Stmt::Kind::Expr: out << pad << expr_to_string(*stmt.expr) << ";\n"; break;
        case Stmt::Kind::Declaration:
            for (const auto& decl : stmt.decls) dump_var(*decl, out, indent);
            break;
        case Stmt::Kind::Block:
            out << pad << "{\n";
            for (const auto& child : stmt.body) dump_statement(*child, out, indent + 2);
            out << pad << "}\n";
            break;
        case Stmt::Kind::If:
            out << pad << "if (" << expr_to_string(*stmt.cond) << ")\n";
            if (!stmt.body.empty()) dump_statement(*stmt.body[0], out, indent + 2);
            if (stmt.else_branch) {
                out << pad << "else\n";
                dump_statement(*stmt.else_branch, out, indent + 2);
            }
            break;
        case Stmt::Kind::For:
            out << pad << "for (...) {\n";
            for (const auto& child : stmt.body) dump_statement(*child, out, indent + 2);
            out << pad << "}\n";
            break;
        case Stmt::Kind::While:
            out << pad << "while (" << expr_to_string(*stmt.cond) << ")\n";
            if (!stmt.body.empty()) dump_statement(*stmt.body[0], out, indent + 2);
            break;
        case Stmt::Kind::DoWhile:
            out << pad << "do ... while (" << expr_to_string(*stmt.cond) << ");\n";
            break;
        case Stmt::Kind::Return:
            out << pad << "return";
            if (stmt.expr) out << " " << expr_to_string(*stmt.expr);
            out << ";\n";
            break;
        case Stmt::Kind::Break: out << pad << "break;\n"; break;
        case Stmt::Kind::Continue: out << pad << "continue;\n"; break;
        case Stmt::Kind::Discard: out << pad << "discard;\n"; break;
        case Stmt::Kind::Switch:
            out << pad << "switch (" << expr_to_string(*stmt.cond) << ") {\n";
            for (const auto& child : stmt.body) dump_statement(*child, out, indent + 2);
            out << pad << "}\n";
            break;
        case Stmt::Kind::Case:
            out << pad << "case " << expr_to_string(*stmt.expr) << ":\n";
            break;
        case Stmt::Kind::Default:
            out << pad << "default:\n";
            break;
    }
}

std::string ast_dump(const TranslationUnit& unit) {
    std::ostringstream out;
    out << "// GLSL " << unit.version << (unit.es ? " es" : "") << " " << stage_name(unit.stage) << "\n";
    for (const auto& directive : unit.directives) out << "#" << directive << "\n";
    for (const auto& precision : unit.precisions) {
        out << "precision " << precision.precision << " " << precision.type << ";\n";
    }
    for (const auto& s : unit.structs) {
        out << "struct " << s.name << " {\n";
        for (const auto& field : s.fields) dump_var(field, out, 2);
        out << "};\n";
    }
    for (const auto& global : unit.globals) dump_var(global, out, 0);
    for (const auto& fn : unit.functions) {
        out << type_to_string(fn.return_type) << " " << fn.name << "(";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i) out << ", ";
            out << type_to_string(fn.params[i].type);
            if (!fn.params[i].name.empty()) out << " " << fn.params[i].name;
        }
        out << ")";
        if (fn.prototype || !fn.body) {
            out << ";\n";
        } else {
            out << " ";
            dump_statement(*fn.body, out, 0);
        }
    }
    return out.str();
}

} // namespace nexus::shader
