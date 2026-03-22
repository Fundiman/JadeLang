#pragma once
#include "lexer.hpp"
#include "ast.hpp"
#include "error.hpp"
#include <vector>
#include <memory>

namespace jscc {

class Parser {
public:
    Parser(std::vector<Token> tokens, ErrorReporter& reporter);

    Program parse();

private:
    std::vector<Token> m_tokens;
    size_t             m_pos = 0;
    ErrorReporter&     m_reporter;

    // ── token navigation ──────────────────────────────────────────────────
    Token&       peek(int offset = 0);
    Token&       advance();
    bool         check(TokenType type) const;
    bool         match(TokenType type);
    Token        expect(TokenType type, const std::string& msg);
    bool         at_end() const;
    void         skip_newlines();

    Span         span_of(const Token& tok) const;

    // ── top level ─────────────────────────────────────────────────────────
    StmtPtr      parse_package();
    StmtPtr      parse_import();
    StmtPtr      parse_top_level();

    // ── declarations ──────────────────────────────────────────────────────
    StmtPtr      parse_def(bool is_async = false);
    StmtPtr      parse_class(Stmt::Kind kind);
    StmtPtr      parse_type_alias();
    Param        parse_param();
    std::vector<std::string> parse_type_params();

    // ── statements ────────────────────────────────────────────────────────
    StmtPtr      parse_stmt();
    StmtPtr      parse_val_var(bool is_val);
    StmtPtr      parse_return();
    StmtPtr      parse_if();
    StmtPtr      parse_for();
    StmtPtr      parse_while();
    std::vector<StmtPtr> parse_block();

    // ── expressions ───────────────────────────────────────────────────────
    ExprPtr      parse_expr();
    ExprPtr      parse_assign();
    ExprPtr      parse_or();
    ExprPtr      parse_and();
    ExprPtr      parse_equality();
    ExprPtr      parse_comparison();
    ExprPtr      parse_addition();
    ExprPtr      parse_multiplication();
    ExprPtr      parse_unary();
    ExprPtr      parse_postfix();
    ExprPtr      parse_primary();
    ExprPtr      parse_when();
    ExprPtr      parse_tuple_or_paren();

    // ── types ─────────────────────────────────────────────────────────────
    TypePtr      parse_type();
};

} // namespace jscc
