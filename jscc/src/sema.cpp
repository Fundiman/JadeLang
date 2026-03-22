#include "sema.hpp"
#include <sstream>
#include <iostream>

namespace jscc {

// ── naming convention helpers ─────────────────────────────────────────────────

static bool is_pascal_case(const std::string& s) {
    return !s.empty() && std::isupper(s[0]);
}

static bool is_camel_case(const std::string& s) {
    return !s.empty() && std::islower(s[0]);
}

static std::string to_pascal(const std::string& s) {
    if (s.empty()) return s;
    std::string r = s;
    r[0] = std::toupper(r[0]);
    return r;
}

static std::string to_camel(const std::string& s) {
    if (s.empty()) return s;
    std::string r = s;
    r[0] = std::tolower(r[0]);
    return r;
}

// ── JadeType ──────────────────────────────────────────────────────────────────

TypeRef JadeType::make(const std::string& name) {
    auto t   = std::make_shared<JadeType>();
    t->kind  = Kind::PRIMITIVE;
    t->name  = name;
    return t;
}

TypeRef JadeType::make_array(TypeRef inner) {
    auto t   = std::make_shared<JadeType>();
    t->kind  = Kind::ARRAY;
    t->name  = "[]" + inner->to_string();
    t->inner = std::move(inner);
    return t;
}

TypeRef JadeType::make_nullable(TypeRef inner) {
    auto t   = std::make_shared<JadeType>();
    t->kind  = Kind::NULLABLE;
    t->name  = inner->to_string() + "?";
    t->inner = std::move(inner);
    return t;
}

TypeRef JadeType::unknown() {
    auto t  = std::make_shared<JadeType>();
    t->kind = Kind::UNKNOWN;
    t->name = "?";
    return t;
}

std::string JadeType::to_string() const {
    switch (kind) {
        case Kind::PRIMITIVE: return name;
        case Kind::CLASS:     return name;
        case Kind::GENERIC:   return name;
        case Kind::ARRAY:     return "[]" + (inner ? inner->to_string() : "?");
        case Kind::NULLABLE:  return (inner ? inner->to_string() : "?") + "?";
        case Kind::UNKNOWN:   return "?";
        case Kind::TUPLE: {
            std::string s = "(";
            for (size_t i = 0; i < params.size(); i++) {
                if (i) s += ", ";
                s += params[i]->to_string();
            }
            return s + ")";
        }
        case Kind::FUNCTION: {
            std::string s = "(";
            for (size_t i = 0; i < params.size(); i++) {
                if (i) s += ", ";
                s += params[i]->to_string();
            }
            s += ") -> ";
            s += return_type ? return_type->to_string() : "void";
            return s;
        }
    }
    return "?";
}

bool JadeType::equals(const JadeType& other) const {
    if (kind == Kind::UNKNOWN || other.kind == Kind::UNKNOWN) return true; // unknown is compatible with anything
    if (kind != other.kind) return false;
    if (name != other.name) return false;
    return true;
}

// ── Scope ─────────────────────────────────────────────────────────────────────

void Scope::define(const std::string& name, Symbol sym) {
    m_symbols[name] = std::move(sym);
}

Symbol* Scope::lookup(const std::string& name) {
    auto it = m_symbols.find(name);
    if (it != m_symbols.end()) return &it->second;
    if (m_parent) return m_parent->lookup(name);
    return nullptr;
}

bool Scope::defined_here(const std::string& name) const {
    return m_symbols.count(name) > 0;
}

// ── Sema ──────────────────────────────────────────────────────────────────────

Sema::Sema(ErrorReporter& reporter) : m_reporter(reporter) {
    // pre-create global scope so import resolver can inject into it
    m_scope = new Scope(nullptr);
    m_int   = JadeType::make("int");
    m_float = JadeType::make("float");
    m_str   = JadeType::make("str");
    m_bool  = JadeType::make("bool");
    m_void  = JadeType::make("void");
    m_null  = JadeType::make("null");
}

void Sema::push_scope() {
    m_scope = new Scope(m_scope);
}

void Sema::pop_scope() {
    auto old = m_scope;
    m_scope  = m_scope->parent();
    delete old;
}

void Sema::define_builtins() {
    // built-in types as symbols so they resolve in type exprs
    for (auto& name : {"int","u8","u16","u32","u64","i8","i16","i32","i64",
                        "f32","f64","float","str","bool","void","null","any"}) {
        m_scope->define(name, { Symbol::Kind::CLASS, name, JadeType::make(name) });
    }

    // built-in functions
    auto str_arr = JadeType::make_array(m_str);
    m_scope->define("println", { Symbol::Kind::FUNCTION, "println", m_void });
    m_scope->define("print",   { Symbol::Kind::FUNCTION, "print",   m_void });
    m_scope->define("readln",  { Symbol::Kind::FUNCTION, "readln",  m_str  });
}

void Sema::analyze(Program& program) {
    define_builtins(); // safe to call multiple times — scope already exists

    for (auto& stmt : program.stmts)
        check_stmt(stmt.get());
}

// ── type resolution ───────────────────────────────────────────────────────────

TypeRef Sema::resolve_type(const TypeExpr* te) {
    if (!te) return m_void;

    switch (te->kind) {
        case TypeExpr::Kind::NAMED: {
            // look up the type name
            auto* sym = m_scope->lookup(te->name);
            if (!sym) {
                // treat as a generic type param or user class (forward ref)
                auto t  = std::make_shared<JadeType>();
                t->kind = JadeType::Kind::CLASS;
                t->name = te->name;
                return t;
            }
            return sym->type;
        }
        case TypeExpr::Kind::ARRAY:
            return JadeType::make_array(resolve_type(te->inner.get()));
        case TypeExpr::Kind::NULLABLE:
            return JadeType::make_nullable(resolve_type(te->inner.get()));
        case TypeExpr::Kind::GENERIC: {
            auto t   = std::make_shared<JadeType>();
            t->kind  = JadeType::Kind::CLASS;
            t->name  = te->name;
            for (auto& p : te->params)
                t->params.push_back(resolve_type(p.get()));
            return t;
        }
        case TypeExpr::Kind::TUPLE: {
            auto t  = std::make_shared<JadeType>();
            t->kind = JadeType::Kind::TUPLE;
            for (auto& p : te->params)
                t->params.push_back(resolve_type(p.get()));
            return t;
        }
    }
    return JadeType::unknown();
}

// ── compatibility ─────────────────────────────────────────────────────────────

bool Sema::types_compatible(const TypeRef& a, const TypeRef& b) {
    if (!a || !b) return true;
    if (a->is_unknown() || b->is_unknown()) return true;
    // null is compatible with any nullable
    if (a->name == "null" && b->kind == JadeType::Kind::NULLABLE) return true;
    if (b->name == "null" && a->kind == JadeType::Kind::NULLABLE) return true;
    return a->equals(*b);
}

// ── span helpers ──────────────────────────────────────────────────────────────

Span Sema::span_of_expr(const Expr* e) const {
    return { m_reporter.filename(), e->line, e->col, e->col + 1 };
}

Span Sema::span_of_stmt(const Stmt* s) const {
    return { m_reporter.filename(), s->line, s->col, s->col + 1 };
}

// ── expression type checker ───────────────────────────────────────────────────

TypeRef Sema::check_expr(Expr* expr) {
    if (!expr) return m_void;

    switch (expr->kind) {
        case Expr::Kind::INT_LIT:    return m_int;
        case Expr::Kind::FLOAT_LIT:  return m_float;
        case Expr::Kind::STRING_LIT: return m_str;
        case Expr::Kind::BOOL_LIT:   return m_bool;
        case Expr::Kind::NULL_LIT:   return m_null;

        case Expr::Kind::IDENT: {
            auto* sym = m_scope->lookup(expr->str_val);
            if (!sym) {
                m_reporter.error("JD301",
                    "undefined variable '" + expr->str_val + "'",
                    span_of_expr(expr),
                    "did you forget to declare it with 'val' or 'var'?");
                return JadeType::unknown();
            }
            return sym->type;
        }

        case Expr::Kind::ASSIGN: {
            auto* sym = m_scope->lookup(expr->left->str_val);
            if (sym && sym->kind == Symbol::Kind::VAL) {
                m_reporter.error("JD302",
                    "cannot assign to 'val' '" + expr->left->str_val + "' — it is immutable",
                    span_of_expr(expr->left.get()),
                    "change 'val' to 'var' if you need mutability");
            }
            auto rhs = check_expr(expr->right.get());
            return rhs;
        }

        case Expr::Kind::BINARY: {
            auto left  = check_expr(expr->left.get());
            auto right = check_expr(expr->right.get());
            auto& op   = expr->op;

            // comparison ops always return bool
            if (op == "==" || op == "!=" || op == "<" ||
                op == ">"  || op == "<=" || op == ">=")
                return m_bool;

            // logical ops require bool operands
            if (op == "&&" || op == "||") {
                if (!types_compatible(left, m_bool))
                    m_reporter.error("JD201", "expected bool operand for '" + op + "'",
                                     span_of_expr(expr->left.get()));
                if (!types_compatible(right, m_bool))
                    m_reporter.error("JD201", "expected bool operand for '" + op + "'",
                                     span_of_expr(expr->right.get()));
                return m_bool;
            }

            // arithmetic: operands should match
            if (!types_compatible(left, right)) {
                m_reporter.error("JD201",
                    "type mismatch in binary '" + op + "': " +
                    left->to_string() + " vs " + right->to_string(),
                    span_of_expr(expr),
                    "ensure both sides have the same type");
            }
            return left;
        }

        case Expr::Kind::UNARY: {
            auto operand = check_expr(expr->left.get());
            if (expr->op == "!" && !types_compatible(operand, m_bool))
                m_reporter.error("JD201", "'!' operator requires bool operand",
                                 span_of_expr(expr->left.get()));
            return operand;
        }

        case Expr::Kind::CALL: {
            auto* sym = m_scope->lookup(expr->str_val);
            if (!sym) {
                // PascalCase names are constructors or pattern variants
                // (e.g. Ok(v), Err(e) in when arms) — not errors
                if (!expr->str_val.empty() && std::isupper(expr->str_val[0])) {
                    for (auto& arg : expr->args) check_expr(arg.get());
                    auto t   = std::make_shared<JadeType>();
                    t->kind  = JadeType::Kind::CLASS;
                    t->name  = expr->str_val;
                    return t;
                }
                m_reporter.error("JD301",
                    "undefined function '" + expr->str_val + "'",
                    span_of_expr(expr));
                return JadeType::unknown();
            }
            for (auto& arg : expr->args) check_expr(arg.get());
            return sym->type ? sym->type : JadeType::unknown();
        }

        case Expr::Kind::METHOD_CALL: {
            check_expr(expr->callee.get());
            for (auto& arg : expr->args) check_expr(arg.get());
            // method return type is unknown at this stage (no full type inference yet)
            return JadeType::unknown();
        }

        case Expr::Kind::FIELD: {
            check_expr(expr->callee.get());
            return JadeType::unknown();
        }

        case Expr::Kind::INDEX: {
            auto arr_type = check_expr(expr->left.get());
            check_expr(expr->right.get());
            if (arr_type->kind == JadeType::Kind::ARRAY)
                return arr_type->inner ? arr_type->inner : JadeType::unknown();
            return JadeType::unknown();
        }

        case Expr::Kind::TUPLE: {
            auto t  = std::make_shared<JadeType>();
            t->kind = JadeType::Kind::TUPLE;
            for (auto& el : expr->elements)
                t->params.push_back(check_expr(el.get()));
            return t;
        }

        case Expr::Kind::WHEN: {
            check_expr(expr->when_subject.get());
            TypeRef result_type = JadeType::unknown();
            for (auto& arm : expr->when_arms) {
                // push a scope for each arm so bindings don't leak
                push_scope();
                // pattern is e.g. Call(Ok) with args [Ident(v)]
                // inject each arg as a val binding in this arm's scope
                auto* pattern = arm.first.get();
                if (pattern && pattern->kind == Expr::Kind::CALL) {
                    for (auto& arg : pattern->args) {
                        if (arg && arg->kind == Expr::Kind::IDENT) {
                            m_scope->define(arg->str_val, {
                                Symbol::Kind::VAL, arg->str_val,
                                JadeType::unknown(), false,
                                arg->line, arg->col
                            });
                        }
                    }
                }
                check_expr(arm.first.get());
                auto arm_type = check_expr(arm.second.get());
                pop_scope();
                if (result_type->is_unknown()) result_type = arm_type;
            }
            return result_type;
        }

        default:
            return JadeType::unknown();
    }
}

// ── statement checker ─────────────────────────────────────────────────────────

void Sema::check_stmt(Stmt* stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case Stmt::Kind::PACKAGE:
        case Stmt::Kind::IMPORT:
            break; // nothing to check yet

        case Stmt::Kind::VAL:
        case Stmt::Kind::VAR: {
            bool is_val = (stmt->kind == Stmt::Kind::VAL);

            // resolve declared type if present
            TypeRef declared = stmt->var_type
                ? resolve_type(stmt->var_type->get())
                : JadeType::unknown();

            // check initializer
            TypeRef init_type = stmt->var_init
                ? check_expr(stmt->var_init.get())
                : JadeType::unknown();

            // type mismatch check
            if (!declared->is_unknown() && !init_type->is_unknown()) {
                if (!types_compatible(declared, init_type)) {
                    m_reporter.error("JD201",
                        "type mismatch: declared '" + declared->to_string() +
                        "' but initializer is '" + init_type->to_string() + "'",
                        span_of_stmt(stmt),
                        "change the type annotation or the initializer");
                }
            }

            // pick the best type: prefer declared, fall back to inferred
            TypeRef final_type = declared->is_unknown() ? init_type : declared;

            // redeclaration check
            if (m_scope->defined_here(stmt->var_name)) {
                m_reporter.error("JD303",
                    "'" + stmt->var_name + "' is already declared in this scope",
                    span_of_stmt(stmt));
            }

            m_scope->define(stmt->var_name, {
                is_val ? Symbol::Kind::VAL : Symbol::Kind::VAR,
                stmt->var_name,
                final_type,
                !is_val,
                stmt->line, stmt->col
            });
            break;
        }

        case Stmt::Kind::RETURN: {
            if (stmt->return_val)
                check_expr(stmt->return_val->get());
            break;
        }

        case Stmt::Kind::EXPR_STMT:
            check_expr(stmt->expr.get());
            break;

        case Stmt::Kind::IF: {
            auto cond_type = check_expr(stmt->if_cond.get());
            if (!types_compatible(cond_type, m_bool)) {
                m_reporter.warning("JD501",
                    "if condition is not a bool — got '" + cond_type->to_string() + "'",
                    span_of_expr(stmt->if_cond.get()));
            }
            push_scope();
            check_block(stmt->if_body);
            pop_scope();
            if (!stmt->else_body.empty()) {
                push_scope();
                check_block(stmt->else_body);
                pop_scope();
            }
            break;
        }

        case Stmt::Kind::FOR: {
            check_expr(stmt->for_iter.get());
            push_scope();
            // define loop variable as unknown type (would need full type inference)
            m_scope->define(stmt->for_var, {
                Symbol::Kind::VAL, stmt->for_var, JadeType::unknown(),
                false, stmt->line, stmt->col
            });
            check_block(stmt->for_body);
            pop_scope();
            break;
        }

        case Stmt::Kind::WHILE: {
            check_expr(stmt->while_cond.get());
            push_scope();
            check_block(stmt->while_body);
            pop_scope();
            break;
        }

        case Stmt::Kind::TYPE_ALIAS: {
            if (!is_pascal_case(stmt->alias_name)) {
                m_reporter.warning("JD602",
                    "type alias '" + stmt->alias_name + "' should be PascalCase",
                    span_of_stmt(stmt),
                    "rename to '" + to_pascal(stmt->alias_name) + "'");
            }
            auto resolved = resolve_type(stmt->alias_type.get());
            m_scope->define(stmt->alias_name, {
                Symbol::Kind::CLASS, stmt->alias_name, resolved,
                false, stmt->line, stmt->col
            });
            break;
        }

        case Stmt::Kind::DEF:
            check_def(stmt);
            break;

        case Stmt::Kind::CLASS:
        case Stmt::Kind::DATA_CLASS:
        case Stmt::Kind::SEALED_CLASS:
        case Stmt::Kind::TRAIT:
            check_class(stmt);
            break;

        default:
            break;
    }
}

void Sema::check_def(Stmt* stmt) {
    // naming convention: functions should be camelCase
    if (!is_camel_case(stmt->fn_name)) {
        m_reporter.warning("JD601",
            "function '" + stmt->fn_name + "' should be camelCase",
            span_of_stmt(stmt),
            "rename to '" + to_camel(stmt->fn_name) + "'");
    }

    // register function in current scope BEFORE checking body (allows recursion)
    TypeRef ret = stmt->fn_return_type
        ? resolve_type(stmt->fn_return_type->get())
        : JadeType::unknown();

    m_scope->define(stmt->fn_name, {
        Symbol::Kind::FUNCTION, stmt->fn_name, ret,
        false, stmt->line, stmt->col
    });

    push_scope();

    // register type params as generic symbols
    for (auto& tp : stmt->type_params)
        m_scope->define(tp, { Symbol::Kind::CLASS, tp, JadeType::make(tp) });

    // register params
    for (auto& p : stmt->fn_params) {
        TypeRef pt = p.type ? resolve_type(p.type->get()) : JadeType::unknown();
        m_scope->define(p.name, {
            Symbol::Kind::PARAM, p.name, pt, false, stmt->line, stmt->col
        });
    }

    if (stmt->fn_expr) {
        // single expression def
        auto expr_type = check_expr(stmt->fn_expr.get());
        if (!ret->is_unknown() && !types_compatible(ret, expr_type)) {
            m_reporter.error("JD201",
                "return type mismatch in '" + stmt->fn_name +
                "': declared '" + ret->to_string() +
                "' but returns '" + expr_type->to_string() + "'",
                span_of_stmt(stmt));
        }
    } else {
        check_block(stmt->fn_body);
    }

    pop_scope();
}

void Sema::check_class(Stmt* stmt) {
    // naming convention: classes should be PascalCase
    if (!is_pascal_case(stmt->class_name)) {
        m_reporter.warning("JD602",
            "class '" + stmt->class_name + "' should be PascalCase",
            span_of_stmt(stmt),
            "rename to '" + to_pascal(stmt->class_name) + "'");
    }

    // register class name
    auto class_type  = std::make_shared<JadeType>();
    class_type->kind = JadeType::Kind::CLASS;
    class_type->name = stmt->class_name;

    m_scope->define(stmt->class_name, {
        Symbol::Kind::CLASS, stmt->class_name, class_type,
        false, stmt->line, stmt->col
    });

    push_scope();

    // type params
    for (auto& tp : stmt->class_type_params)
        m_scope->define(tp, { Symbol::Kind::CLASS, tp, JadeType::make(tp) });

    check_block(stmt->class_body);

    pop_scope();
}

void Sema::check_block(std::vector<StmtPtr>& body) {
    for (auto& stmt : body)
        check_stmt(stmt.get());
}

} // namespace jscc
