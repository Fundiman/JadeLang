#include "parser.hpp"
#include <stdexcept>
#include <iostream>

namespace jscc {

Parser::Parser(std::vector<Token> tokens, ErrorReporter& reporter)
    : m_tokens(std::move(tokens)), m_reporter(reporter) {}

// ── token navigation ──────────────────────────────────────────────────────────

Token& Parser::peek(int offset) {
    size_t idx = m_pos + offset;
    if (idx >= m_tokens.size()) return m_tokens.back(); // EOF
    return m_tokens[idx];
}

Token& Parser::advance() {
    if (!at_end()) m_pos++;
    return m_tokens[m_pos - 1];
}

bool Parser::check(TokenType type) const {
    if (m_pos >= m_tokens.size()) return false;
    return m_tokens[m_pos].type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) { advance(); return true; }
    return false;
}

Token Parser::expect(TokenType type, const std::string& msg) {
    if (check(type)) return advance();
    auto& tok = peek();
    m_reporter.error("JD101", msg + " — got '" + tok.value + "'",
                     { m_reporter.filename(), tok.line, tok.col, tok.col + (int)tok.value.size() });
    // return a dummy token so parsing can continue
    return { type, "", tok.line, tok.col };
}

bool Parser::at_end() const {
    return m_pos >= m_tokens.size() ||
           m_tokens[m_pos].type == TokenType::END_OF_FILE;
}

void Parser::skip_newlines() {
    while (check(TokenType::NEWLINE)) advance();
}

Span Parser::span_of(const Token& tok) const {
    return { m_reporter.filename(), tok.line, tok.col,
             tok.col + (int)tok.value.size() };
}

// ── top level ─────────────────────────────────────────────────────────────────

Program Parser::parse() {
    Program prog;
    skip_newlines();

    // optional package declaration
    if (check(TokenType::KW_PACKAGE)) {
        auto pkg = parse_package();
        prog.package_name = pkg->path;
    }

    skip_newlines();

    while (!at_end()) {
        skip_newlines();
        if (at_end()) break;
        prog.stmts.push_back(parse_top_level());
        skip_newlines();
    }

    return prog;
}

StmtPtr Parser::parse_package() {
    auto tok = advance(); // consume 'package'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::PACKAGE;
    stmt->line = tok.line; stmt->col = tok.col;

    // collect dotted path: jade.stdlib.io
    stmt->path = expect(TokenType::IDENT, "expected package name").value;
    while (check(TokenType::DOT)) {
        advance();
        stmt->path += "." + expect(TokenType::IDENT, "expected identifier after '.'").value;
    }
    match(TokenType::SEMICOLON);
    return stmt;
}

StmtPtr Parser::parse_import() {
    auto tok = advance(); // consume 'import'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::IMPORT;
    stmt->line = tok.line; stmt->col = tok.col;

    stmt->path = expect(TokenType::IDENT, "expected import path").value;
    while (check(TokenType::DOT)) {
        advance();
        // could be .{ Thing, Other } or .next.segment
        if (check(TokenType::LBRACE)) {
            advance();
            skip_newlines();
            while (!check(TokenType::RBRACE) && !at_end()) {
                stmt->imports.push_back(
                    expect(TokenType::IDENT, "expected import name").value);
                if (!match(TokenType::COMMA)) break;
                skip_newlines();
            }
            expect(TokenType::RBRACE, "expected '}' after import list");
            break;
        }
        stmt->path += "." + expect(TokenType::IDENT, "expected identifier after '.'").value;
    }
    match(TokenType::SEMICOLON);
    return stmt;
}

StmtPtr Parser::parse_top_level() {
    skip_newlines();
    auto& tok = peek();

    if (tok.type == TokenType::KW_IMPORT)       return parse_import();
    if (tok.type == TokenType::KW_ASYNC) {
        advance();
        return parse_def(true);
    }
    if (tok.type == TokenType::KW_DEF)          return parse_def();
    if (tok.type == TokenType::KW_DATA)         return parse_class(Stmt::Kind::DATA_CLASS);
    if (tok.type == TokenType::KW_SEALED)       return parse_class(Stmt::Kind::SEALED_CLASS);
    if (tok.type == TokenType::KW_CLASS)        return parse_class(Stmt::Kind::CLASS);
    if (tok.type == TokenType::KW_TRAIT)        return parse_class(Stmt::Kind::TRAIT);
    if (tok.type == TokenType::KW_TYPE)         return parse_type_alias();

    return parse_stmt();
}

// ── declarations ──────────────────────────────────────────────────────────────

std::vector<std::string> Parser::parse_type_params() {
    std::vector<std::string> params;
    if (!check(TokenType::LANGLE)) return params;
    advance(); // <
    while (!check(TokenType::RANGLE) && !at_end()) {
        params.push_back(expect(TokenType::IDENT, "expected type parameter").value);
        if (!match(TokenType::COMMA)) break;
    }
    expect(TokenType::RANGLE, "expected '>' after type parameters");
    return params;
}

Param Parser::parse_param() {
    Param p;
    p.name = expect(TokenType::IDENT, "expected parameter name").value;

    // optional type annotation: name Type
    if (check(TokenType::IDENT) || check(TokenType::LBRACKET)) {
        p.type = parse_type();
    }

    // optional default: name Type = expr
    if (match(TokenType::ASSIGN)) {
        p.default_val = parse_expr();
    }

    return p;
}

StmtPtr Parser::parse_def(bool is_async) {
    auto tok = advance(); // consume 'def'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind     = Stmt::Kind::DEF;
    stmt->is_async = is_async;
    stmt->line = tok.line; stmt->col = tok.col;

    stmt->fn_name      = expect(TokenType::IDENT, "expected function name").value;
    stmt->type_params  = parse_type_params();

    // parameter list
    expect(TokenType::LPAREN, "expected '(' after function name");
    while (!check(TokenType::RPAREN) && !at_end()) {
        stmt->fn_params.push_back(parse_param());
        if (!match(TokenType::COMMA)) break;
    }
    expect(TokenType::RPAREN, "expected ')' after parameters");

    // optional return type
    if (!check(TokenType::ASSIGN) && !check(TokenType::LBRACE) &&
        !check(TokenType::NEWLINE) && !at_end()) {
        stmt->fn_return_type = parse_type();
    }

    // body: = expr  OR  { stmts }
    if (match(TokenType::ASSIGN)) {
        stmt->fn_expr = parse_expr();
        match(TokenType::SEMICOLON); // optional ; after single-expr def
    } else {
        stmt->fn_body = parse_block();
    }

    return stmt;
}

StmtPtr Parser::parse_class(Stmt::Kind kind) {
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = kind;

    if (kind == Stmt::Kind::DATA_CLASS) {
        advance(); // 'data'
        advance(); // 'class'
    } else if (kind == Stmt::Kind::SEALED_CLASS) {
        advance(); // 'sealed'
        advance(); // 'class'
    } else {
        auto tok = advance(); // 'class' or 'trait'
        stmt->line = tok.line; stmt->col = tok.col;
    }

    stmt->class_name        = expect(TokenType::IDENT, "expected class name").value;
    stmt->class_type_params = parse_type_params();

    // primary constructor: data class Ok(val value T)
    if (check(TokenType::LPAREN)) {
        advance();
        while (!check(TokenType::RPAREN) && !at_end()) {
            bool fval = true;
            if      (check(TokenType::KW_VAL)) { advance(); fval = true;  }
            else if (check(TokenType::KW_VAR)) { advance(); fval = false; }
            auto field = std::make_unique<Stmt>();
            field->kind     = fval ? Stmt::Kind::VAL : Stmt::Kind::VAR;
            field->line     = peek().line; field->col = peek().col;
            field->var_name = expect(TokenType::IDENT, "expected field name").value;
            if (!check(TokenType::COMMA) && !check(TokenType::RPAREN) && !at_end())
                field->var_type = parse_type();
            stmt->class_body.push_back(std::move(field));
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RPAREN, "expected ')' after primary constructor");
        // optional extra body
        if (check(TokenType::LBRACE)) {
            auto extra = parse_block();
            for (auto& s : extra) stmt->class_body.push_back(std::move(s));
        }
        return stmt;
    }

    stmt->class_body = parse_block();
    return stmt;
}

// ── statements ────────────────────────────────────────────────────────────────

StmtPtr Parser::parse_type_alias() {
    auto tok = advance(); // consume 'type'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::TYPE_ALIAS;
    stmt->line = tok.line; stmt->col = tok.col;

    stmt->alias_name = expect(TokenType::IDENT, "expected type alias name").value;
    expect(TokenType::ASSIGN, "expected '=' after type alias name");
    stmt->alias_type = parse_type();
    expect(TokenType::SEMICOLON, "expected ';' after type alias");
    return stmt;
}

std::vector<StmtPtr> Parser::parse_block() {
    expect(TokenType::LBRACE, "expected '{'");
    std::vector<StmtPtr> stmts;
    skip_newlines();
    while (!check(TokenType::RBRACE) && !at_end()) {
        stmts.push_back(parse_stmt());
        skip_newlines();
    }
    expect(TokenType::RBRACE, "expected '}'");
    return stmts;
}

StmtPtr Parser::parse_stmt() {
    skip_newlines();
    auto& tok = peek();

    switch (tok.type) {
        case TokenType::KW_VAL:    return parse_val_var(true);
        case TokenType::KW_VAR:    return parse_val_var(false);
        case TokenType::KW_RETURN: return parse_return();
        case TokenType::KW_IF:     return parse_if();
        case TokenType::KW_FOR:    return parse_for();
        case TokenType::KW_WHILE:  return parse_while();
        case TokenType::KW_DEF:    return parse_def();
        case TokenType::KW_ASYNC: {
            advance();
            return parse_def(true);
        }
        case TokenType::KW_DATA:   return parse_class(Stmt::Kind::DATA_CLASS);
        case TokenType::KW_SEALED: return parse_class(Stmt::Kind::SEALED_CLASS);
        case TokenType::KW_CLASS:  return parse_class(Stmt::Kind::CLASS);
        default: break;
    }

    // expression statement
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::EXPR_STMT;
    stmt->line = tok.line; stmt->col = tok.col;
    stmt->expr = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after statement");
    return stmt;
}

StmtPtr Parser::parse_val_var(bool is_val) {
    auto tok = advance(); // consume val/var
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = is_val ? Stmt::Kind::VAL : Stmt::Kind::VAR;
    stmt->line = tok.line; stmt->col = tok.col;

    stmt->var_name = expect(TokenType::IDENT, "expected variable name").value;

    // optional type annotation
    if (!check(TokenType::ASSIGN) && !check(TokenType::NEWLINE) &&
        !check(TokenType::RPAREN) && !check(TokenType::RBRACE) && !at_end()) {
        stmt->var_type = parse_type();
    }

    // class body fields may omit = initializer: val x T
    if (!check(TokenType::ASSIGN)) {
        // field declaration in class body — no semicolon required
        return stmt;
    }

    advance(); // consume '='
    stmt->var_init = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after variable declaration");
    return stmt;
}

StmtPtr Parser::parse_return() {
    auto tok = advance(); // consume 'return'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::RETURN;
    stmt->line = tok.line; stmt->col = tok.col;

    if (!check(TokenType::SEMICOLON) && !check(TokenType::RBRACE) && !at_end())
        stmt->return_val = parse_expr();
    match(TokenType::SEMICOLON);
    return stmt;
}

StmtPtr Parser::parse_if() {
    auto tok = advance(); // consume 'if'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::IF;
    stmt->line = tok.line; stmt->col = tok.col;

    stmt->if_cond = parse_expr();
    stmt->if_body = parse_block();

    skip_newlines();
    if (match(TokenType::KW_ELSE)) {
        if (check(TokenType::KW_IF)) {
            stmt->else_body.push_back(parse_if());
        } else {
            stmt->else_body = parse_block();
        }
    }
    return stmt;
}

StmtPtr Parser::parse_for() {
    auto tok = advance(); // consume 'for'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind = Stmt::Kind::FOR;
    stmt->line = tok.line; stmt->col = tok.col;

    stmt->for_var  = expect(TokenType::IDENT, "expected loop variable").value;
    expect(TokenType::KW_IN, "expected 'in' after loop variable");
    stmt->for_iter = parse_expr();
    stmt->for_body = parse_block();
    return stmt;
}

StmtPtr Parser::parse_while() {
    auto tok = advance(); // consume 'while'
    auto stmt = std::make_unique<Stmt>();
    stmt->kind  = Stmt::Kind::WHILE;
    stmt->line = tok.line; stmt->col = tok.col;

    stmt->while_cond = parse_expr();
    stmt->while_body = parse_block();
    return stmt;
}

// ── types ─────────────────────────────────────────────────────────────────────

TypePtr Parser::parse_type() {
    auto type = std::make_unique<TypeExpr>();
    type->line = peek().line; type->col = peek().col;

    // []T — array type
    if (check(TokenType::LBRACKET)) {
        advance();
        expect(TokenType::RBRACKET, "expected ']'");
        type->kind  = TypeExpr::Kind::ARRAY;
        type->inner = parse_type();
        return type;
    }

    // (T, U) — tuple type
    if (check(TokenType::LPAREN)) {
        advance();
        type->kind = TypeExpr::Kind::TUPLE;
        while (!check(TokenType::RPAREN) && !at_end()) {
            type->params.push_back(parse_type());
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RPAREN, "expected ')'");
        return type;
    }

    // function type: def(T, U) R
    if (check(TokenType::KW_DEF)) {
        advance();
        type->kind = TypeExpr::Kind::NAMED;
        type->name = "def";
        expect(TokenType::LPAREN, "expected '(' in function type");
        while (!check(TokenType::RPAREN) && !at_end()) {
            type->params.push_back(parse_type());
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RPAREN, "expected ')' in function type");
        // optional return type
        if (!check(TokenType::SEMICOLON) && !check(TokenType::COMMA) &&
            !check(TokenType::RPAREN) && !check(TokenType::NEWLINE) && !at_end()) {
            type->inner = parse_type();
        }
        return type;
    }

    type->kind = TypeExpr::Kind::NAMED;
    type->name = expect(TokenType::IDENT, "expected type name").value;

    // Generic: Name<T, U>
    if (check(TokenType::LANGLE)) {
        advance();
        type->kind = TypeExpr::Kind::GENERIC;
        while (!check(TokenType::RANGLE) && !at_end()) {
            type->params.push_back(parse_type());
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RANGLE, "expected '>' after type parameters");
    }

    // Nullable: Type?
    if (match(TokenType::QUESTION)) {
        auto nullable = std::make_unique<TypeExpr>();
        nullable->kind  = TypeExpr::Kind::NULLABLE;
        nullable->inner = std::move(type);
        return nullable;
    }

    return type;
}

// ── expressions ───────────────────────────────────────────────────────────────

ExprPtr Parser::parse_expr()           { return parse_assign(); }

ExprPtr Parser::parse_assign() {
    auto left = parse_or();
    if (check(TokenType::ASSIGN)) {
        auto tok = advance();
        auto right = parse_assign();
        auto expr = std::make_unique<Expr>();
        expr->kind  = Expr::Kind::ASSIGN;
        expr->op    = "=";
        expr->left  = std::move(left);
        expr->right = std::move(right);
        expr->line  = tok.line; expr->col = tok.col;
        return expr;
    }
    return left;
}

ExprPtr Parser::parse_or() {
    auto left = parse_and();
    while (check(TokenType::OR)) {
        auto tok = advance();
        auto right = parse_and();
        auto expr = std::make_unique<Expr>();
        expr->kind  = Expr::Kind::BINARY;
        expr->op    = "||";
        expr->left  = std::move(left);
        expr->right = std::move(right);
        expr->line  = tok.line; expr->col = tok.col;
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::parse_and() {
    auto left = parse_equality();
    while (check(TokenType::AND)) {
        auto tok = advance();
        auto right = parse_equality();
        auto expr = std::make_unique<Expr>();
        expr->kind  = Expr::Kind::BINARY;
        expr->op    = "&&";
        expr->left  = std::move(left);
        expr->right = std::move(right);
        expr->line  = tok.line; expr->col = tok.col;
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::parse_equality() {
    auto left = parse_comparison();
    while (check(TokenType::EQ) || check(TokenType::NEQ)) {
        auto tok = advance();
        auto right = parse_comparison();
        auto expr = std::make_unique<Expr>();
        expr->kind  = Expr::Kind::BINARY;
        expr->op    = tok.value;
        expr->left  = std::move(left);
        expr->right = std::move(right);
        expr->line  = tok.line; expr->col = tok.col;
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::parse_comparison() {
    auto left = parse_addition();
    while (check(TokenType::LT)  || check(TokenType::GT) ||
           check(TokenType::LTE) || check(TokenType::GTE)) {
        auto tok = advance();
        auto right = parse_addition();
        auto expr = std::make_unique<Expr>();
        expr->kind  = Expr::Kind::BINARY;
        expr->op    = tok.value;
        expr->left  = std::move(left);
        expr->right = std::move(right);
        expr->line  = tok.line; expr->col = tok.col;
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::parse_addition() {
    auto left = parse_multiplication();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        auto tok = advance();
        auto right = parse_multiplication();
        auto expr = std::make_unique<Expr>();
        expr->kind  = Expr::Kind::BINARY;
        expr->op    = tok.value;
        expr->left  = std::move(left);
        expr->right = std::move(right);
        expr->line  = tok.line; expr->col = tok.col;
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::parse_multiplication() {
    auto left = parse_unary();
    while (check(TokenType::STAR) || check(TokenType::SLASH) || check(TokenType::PERCENT)) {
        auto tok = advance();
        auto right = parse_unary();
        auto expr = std::make_unique<Expr>();
        expr->kind  = Expr::Kind::BINARY;
        expr->op    = tok.value;
        expr->left  = std::move(left);
        expr->right = std::move(right);
        expr->line  = tok.line; expr->col = tok.col;
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::parse_unary() {
    if (check(TokenType::NOT) || check(TokenType::MINUS)) {
        auto tok = advance();
        auto expr = std::make_unique<Expr>();
        expr->kind  = Expr::Kind::UNARY;
        expr->op    = tok.value;
        expr->left  = parse_unary();
        expr->line  = tok.line; expr->col = tok.col;
        return expr;
    }
    return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
    auto expr = parse_primary();

    while (true) {
        if (check(TokenType::DOT)) {
            advance();
            // .await() is a keyword used as a postfix — handle specially
            std::string name;
            if (check(TokenType::KW_AWAIT)) {
                name = "await";
                advance();
            } else {
                name = expect(TokenType::IDENT, "expected field or method name").value;
            }

            // method call: expr.name(args)
            if (check(TokenType::LPAREN)) {
                advance();
                auto call = std::make_unique<Expr>();
                call->kind        = Expr::Kind::METHOD_CALL;
                call->callee      = std::move(expr);
                call->method_name = name;
                call->line        = peek().line; call->col = peek().col;

                while (!check(TokenType::RPAREN) && !at_end()) {
                    call->args.push_back(parse_expr());
                    if (!match(TokenType::COMMA)) break;
                }
                expect(TokenType::RPAREN, "expected ')'");
                expr = std::move(call);
            } else {
                // field access: expr.name
                auto field = std::make_unique<Expr>();
                field->kind        = Expr::Kind::FIELD;
                field->callee      = std::move(expr);
                field->method_name = name;
                field->line        = peek().line; field->col = peek().col;
                expr = std::move(field);
            }
        } else if (check(TokenType::LBRACKET)) {
            // index: expr[i]
            advance();
            auto idx = std::make_unique<Expr>();
            idx->kind  = Expr::Kind::INDEX;
            idx->left  = std::move(expr);
            idx->right = parse_expr();
            expect(TokenType::RBRACKET, "expected ']'");
            expr = std::move(idx);
        } else {
            break;
        }
    }

    return expr;
}

ExprPtr Parser::parse_primary() {
    auto& tok = peek();

    // literals
    if (tok.type == TokenType::INT_LIT) {
        auto expr = std::make_unique<Expr>();
        expr->kind    = Expr::Kind::INT_LIT;
        expr->int_val = std::stoll(tok.value);
        expr->line = tok.line; expr->col = tok.col;
        advance();
        return expr;
    }
    if (tok.type == TokenType::FLOAT_LIT) {
        auto expr = std::make_unique<Expr>();
        expr->kind      = Expr::Kind::FLOAT_LIT;
        expr->float_val = std::stod(tok.value);
        expr->line = tok.line; expr->col = tok.col;
        advance();
        return expr;
    }
    if (tok.type == TokenType::STRING_LIT) {
        auto expr = std::make_unique<Expr>();
        expr->kind    = Expr::Kind::STRING_LIT;
        expr->str_val = tok.value;
        expr->line = tok.line; expr->col = tok.col;
        advance();
        return expr;
    }
    if (tok.type == TokenType::KW_TRUE || tok.type == TokenType::KW_FALSE) {
        auto expr = std::make_unique<Expr>();
        expr->kind     = Expr::Kind::BOOL_LIT;
        expr->bool_val = (tok.type == TokenType::KW_TRUE);
        expr->line = tok.line; expr->col = tok.col;
        advance();
        return expr;
    }
    if (tok.type == TokenType::KW_NULL) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::NULL_LIT;
        expr->line = tok.line; expr->col = tok.col;
        advance();
        return expr;
    }

    // when expression
    if (tok.type == TokenType::KW_WHEN) return parse_when();

    // grouped / tuple: (expr) or (a, b)
    if (tok.type == TokenType::LPAREN) return parse_tuple_or_paren();

    // identifier or function call
    if (tok.type == TokenType::IDENT) {
        auto name_tok = advance();
        if (check(TokenType::LPAREN)) {
            // function call
            advance();
            auto expr = std::make_unique<Expr>();
            expr->kind     = Expr::Kind::CALL;
            expr->str_val  = name_tok.value;
            expr->line = name_tok.line; expr->col = name_tok.col;

            while (!check(TokenType::RPAREN) && !at_end()) {
                expr->args.push_back(parse_expr());
                if (!match(TokenType::COMMA)) break;
            }
            expect(TokenType::RPAREN, "expected ')'");
            return expr;
        }

        auto expr = std::make_unique<Expr>();
        expr->kind    = Expr::Kind::IDENT;
        expr->str_val = name_tok.value;
        expr->line = name_tok.line; expr->col = name_tok.col;
        return expr;
    }

    // error recovery
    m_reporter.error("JD102", "unexpected token '" + tok.value + "'",
                     span_of(tok));
    advance();
    auto err = std::make_unique<Expr>();
    err->kind = Expr::Kind::NULL_LIT;
    err->line = tok.line; err->col = tok.col;
    return err;
}

ExprPtr Parser::parse_when() {
    auto tok = advance(); // consume 'when'
    auto expr = std::make_unique<Expr>();
    expr->kind         = Expr::Kind::WHEN;
    expr->when_subject = parse_expr();
    expr->line = tok.line; expr->col = tok.col;

    expect(TokenType::LBRACE, "expected '{' after when subject");
    skip_newlines();

    while (!check(TokenType::RBRACE) && !at_end()) {
        skip_newlines();
        // is Pattern(vars) -> body
        expect(TokenType::KW_IS, "expected 'is' in when arm");
        auto pattern = parse_expr();
        expect(TokenType::ARROW, "expected '->' after when pattern");
        auto body = parse_expr();
        match(TokenType::SEMICOLON); // optional ; after arm body
        expr->when_arms.emplace_back(std::move(pattern), std::move(body));
        skip_newlines();
    }

    expect(TokenType::RBRACE, "expected '}' after when arms");
    return expr;
}

ExprPtr Parser::parse_tuple_or_paren() {
    advance(); // consume '('
    auto first = parse_expr();

    if (match(TokenType::COMMA)) {
        // it's a tuple
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::TUPLE;
        expr->elements.push_back(std::move(first));
        while (!check(TokenType::RPAREN) && !at_end()) {
            expr->elements.push_back(parse_expr());
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RPAREN, "expected ')'");
        return expr;
    }

    expect(TokenType::RPAREN, "expected ')'");
    return first;
}

} // namespace jscc
