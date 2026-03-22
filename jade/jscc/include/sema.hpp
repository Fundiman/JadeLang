#pragma once
#include "ast.hpp"
#include "error.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <memory>

namespace jscc {

// ── type system ───────────────────────────────────────────────────────────────

struct JadeType {
    enum class Kind {
        PRIMITIVE,   // int, str, bool, float, void
        CLASS,       // user defined class
        GENERIC,     // T, U (unresolved type param)
        ARRAY,       // []T
        NULLABLE,    // T?
        TUPLE,       // (T, U)
        FUNCTION,    // (T, U) -> R
        UNKNOWN,     // not yet resolved
    };

    Kind        kind = Kind::UNKNOWN;
    std::string name;                           // "int", "str", "MyClass"
    std::vector<std::shared_ptr<JadeType>> params; // generic params / tuple members
    std::shared_ptr<JadeType> inner;            // array/nullable inner type
    std::shared_ptr<JadeType> return_type;      // for function types

    bool is_void()    const { return kind == Kind::PRIMITIVE && name == "void"; }
    bool is_unknown() const { return kind == Kind::UNKNOWN; }
    bool equals(const JadeType& other) const;
    std::string to_string() const;

    static std::shared_ptr<JadeType> make(const std::string& name);
    static std::shared_ptr<JadeType> make_array(std::shared_ptr<JadeType> inner);
    static std::shared_ptr<JadeType> make_nullable(std::shared_ptr<JadeType> inner);
    static std::shared_ptr<JadeType> unknown();
};

using TypeRef = std::shared_ptr<JadeType>;

// ── symbol table ──────────────────────────────────────────────────────────────

struct Symbol {
    enum class Kind { VAR, VAL, FUNCTION, CLASS, PARAM };
    Kind    kind;
    std::string name;
    TypeRef type;
    bool    is_mutable = false;
    int     line = 0, col = 0;
};

class Scope {
public:
    explicit Scope(Scope* parent = nullptr) : m_parent(parent) {}

    void        define(const std::string& name, Symbol sym);
    Symbol*     lookup(const std::string& name);
    bool        defined_here(const std::string& name) const;
    Scope*      parent() const { return m_parent; }

private:
    Scope*                               m_parent;
    std::unordered_map<std::string, Symbol> m_symbols;
};

// ── semantic analyzer ─────────────────────────────────────────────────────────

class Sema {
public:
    Sema(ErrorReporter& reporter);

    void analyze(Program& program);
    Scope* global_scope() { return m_scope; }

private:
    ErrorReporter& m_reporter;
    Scope*         m_scope = nullptr;

    // built-in types
    TypeRef m_int, m_float, m_str, m_bool, m_void, m_null;

    void push_scope();
    void pop_scope();
    void define_builtins();

    TypeRef resolve_type(const TypeExpr* te);
    TypeRef check_expr(Expr* expr);
    void    check_stmt(Stmt* stmt);
    void    check_def(Stmt* stmt);
    void    check_class(Stmt* stmt);
    void    check_block(std::vector<StmtPtr>& body);

    bool types_compatible(const TypeRef& a, const TypeRef& b);

    Span span_of_expr(const Expr* e) const;
    Span span_of_stmt(const Stmt* s) const;
};

} // namespace jscc
