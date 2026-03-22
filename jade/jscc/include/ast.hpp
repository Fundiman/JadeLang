#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace jscc {

// forward declarations
struct Expr;
struct Stmt;
struct TypeExpr;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using TypePtr = std::unique_ptr<TypeExpr>;

// ─── type expressions ────────────────────────────────────────────────────────

struct TypeExpr {
    enum class Kind {
        NAMED,      // int, str, MyClass
        GENERIC,    // Result<T>, Vec<int>
        ARRAY,      // []int
        NULLABLE,   // int?
        TUPLE,      // (int, str)
    };

    Kind                     kind;
    std::string              name;
    std::vector<TypePtr>     params;   // for generics
    TypePtr                  inner;    // for array/nullable
    int line, col;
};

// ─── expressions ─────────────────────────────────────────────────────────────

struct Expr {
    enum class Kind {
        INT_LIT, FLOAT_LIT, STRING_LIT, BOOL_LIT, NULL_LIT,
        IDENT,
        BINARY,         // a + b
        UNARY,          // !x, -x
        CALL,           // foo(a, b)
        METHOD_CALL,    // obj.method(a, b)
        FIELD,          // obj.field
        INDEX,          // arr[i]
        ASSIGN,         // x = val
        LAMBDA,         // (x) -> expr
        WHEN,           // when x { is Ok(v) -> ... }
        TUPLE,          // (a, b)
        AWAIT,          // expr.await()
        INTERPOLATED,   // "hello #{name}"
    };

    Kind        kind;
    int         line, col;

    // literals
    std::string str_val;
    long long   int_val   = 0;
    double      float_val = 0.0;
    bool        bool_val  = false;

    // binary / unary
    std::string op;
    ExprPtr     left;
    ExprPtr     right;

    // call / method call
    ExprPtr                  callee;
    std::vector<ExprPtr>     args;
    std::string              method_name;

    // when expr
    ExprPtr                              when_subject;
    std::vector<std::pair<ExprPtr, ExprPtr>> when_arms; // pattern -> body

    // tuple
    std::vector<ExprPtr> elements;

    // await
    ExprPtr await_expr;
};

// ─── statements ──────────────────────────────────────────────────────────────

struct Param {
    std::string            name;
    std::optional<TypePtr> type;
    std::optional<ExprPtr> default_val;
};

struct Stmt {
    enum class Kind {
        PACKAGE,        // package jade.hello
        IMPORT,         // import jade.stdlib.io
        VAL,            // val x int = expr
        VAR,            // var x int = expr
        RETURN,         // return expr
        EXPR_STMT,      // expr;
        BLOCK,          // { stmts... }
        IF,             // if cond { } else { }
        FOR,            // for x in expr { }
        WHILE,          // while cond { }
        DEF,            // def foo(args) ReturnType { }
        CLASS,          // class Foo { }
        DATA_CLASS,     // data class Foo { }
        SEALED_CLASS,   // sealed class Foo { }
        TRAIT,          // trait Foo { }
        TYPE_ALIAS,     // type Foo = SomeType
    };

    Kind        kind;
    int         line, col;

    // package / import
    std::string              path;       // "jade.stdlib.io"
    std::vector<std::string> imports;   // specific imports { Thing, Other }

    // val / var
    std::string            var_name;
    std::optional<TypePtr> var_type;
    ExprPtr                var_init;

    // return
    std::optional<ExprPtr> return_val;

    // block
    std::vector<StmtPtr> body;

    // if
    ExprPtr               if_cond;
    std::vector<StmtPtr>  if_body;
    std::vector<StmtPtr>  else_body;

    // for
    std::string           for_var;
    ExprPtr               for_iter;
    std::vector<StmtPtr>  for_body;

    // while
    ExprPtr               while_cond;
    std::vector<StmtPtr>  while_body;

    // def
    bool                   is_async  = false;
    std::string            fn_name;
    std::vector<std::string> type_params;  // <T, U>
    std::vector<Param>     fn_params;
    std::optional<TypePtr> fn_return_type;
    std::vector<StmtPtr>   fn_body;
    ExprPtr                fn_expr;       // single-expression def foo() = expr

    // class / data class / sealed class / trait
    std::string              class_name;
    std::vector<std::string> class_type_params;
    std::vector<StmtPtr>     class_body;

    // type alias
    std::string              alias_name;
    TypePtr                  alias_type;

    // expr stmt
    ExprPtr expr;
};

// ─── top level program ───────────────────────────────────────────────────────

struct Program {
    std::string          package_name;
    std::vector<StmtPtr> stmts;
};

} // namespace jscc
