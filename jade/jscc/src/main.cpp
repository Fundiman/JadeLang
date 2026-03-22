#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include "import_resolver.hpp"
#include "codegen.hpp"
#include "error.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

static const std::string TEST_SOURCE = R"(
package jade.Hello;

import jade.stdlib.io;
import jade.stdlib.net.http;

// type aliases — no demonic syntax allowed
type UserId = u32;
type Handler = def(Request) Response;
type Matrix  = [][]float;

def main(args []str) {
    val message str = "hello from Jade!";
    val x int = 42;
    val y = x + 8;
    io.println("#{message} x=#{x}");
    return 0;
}

data class Point<T> {
    val x T
    val y T

    def distance() = x * x + y * y;
}

sealed class Result<T> {
    data class Ok(val value T)
    data class Err(val msg str)
}

// naming violation — should warn
data class badName {
    val x int
}

async def fetch(url str) {
    val result = http.get(url).await();
    when result {
        is Ok(v)  -> io.println("got: #{v}");
        is Err(e) -> io.println("error: #{e}");
    };
}
)";

// ── simple AST printer ───────────────────────────────────────────────────────

static void print_indent(int depth) {
    for (int i = 0; i < depth; i++) std::cout << "  ";
}

static void print_stmt(const jscc::Stmt* s, int depth = 0);
static void print_expr(const jscc::Expr* e, int depth = 0);

static void print_expr(const jscc::Expr* e, int depth) {
    if (!e) return;
    print_indent(depth);
    switch (e->kind) {
        case jscc::Expr::Kind::INT_LIT:    std::cout << "Int(" << e->int_val << ")\n"; break;
        case jscc::Expr::Kind::FLOAT_LIT:  std::cout << "Float(" << e->float_val << ")\n"; break;
        case jscc::Expr::Kind::STRING_LIT: std::cout << "Str(\"" << e->str_val << "\")\n"; break;
        case jscc::Expr::Kind::BOOL_LIT:   std::cout << "Bool(" << (e->bool_val ? "true" : "false") << ")\n"; break;
        case jscc::Expr::Kind::NULL_LIT:   std::cout << "Null\n"; break;
        case jscc::Expr::Kind::IDENT:      std::cout << "Ident(" << e->str_val << ")\n"; break;
        case jscc::Expr::Kind::CALL:
            std::cout << "Call(" << e->str_val << ")\n";
            for (auto& a : e->args) print_expr(a.get(), depth + 1);
            break;
        case jscc::Expr::Kind::METHOD_CALL:
            std::cout << "MethodCall(." << e->method_name << ")\n";
            print_expr(e->callee.get(), depth + 1);
            for (auto& a : e->args) print_expr(a.get(), depth + 1);
            break;
        case jscc::Expr::Kind::FIELD:
            std::cout << "Field(." << e->method_name << ")\n";
            print_expr(e->callee.get(), depth + 1);
            break;
        case jscc::Expr::Kind::BINARY:
            std::cout << "Binary(" << e->op << ")\n";
            print_expr(e->left.get(),  depth + 1);
            print_expr(e->right.get(), depth + 1);
            break;
        case jscc::Expr::Kind::UNARY:
            std::cout << "Unary(" << e->op << ")\n";
            print_expr(e->left.get(), depth + 1);
            break;
        case jscc::Expr::Kind::WHEN:
            std::cout << "When\n";
            print_expr(e->when_subject.get(), depth + 1);
            for (auto& arm : e->when_arms) {
                print_indent(depth + 1); std::cout << "arm:\n";
                print_expr(arm.first.get(),  depth + 2);
                print_expr(arm.second.get(), depth + 2);
            }
            break;
        case jscc::Expr::Kind::TUPLE:
            std::cout << "Tuple\n";
            for (auto& el : e->elements) print_expr(el.get(), depth + 1);
            break;
        default:
            std::cout << "Expr(?)\n"; break;
    }
}

static void print_stmt(const jscc::Stmt* s, int depth) {
    if (!s) return;
    print_indent(depth);
    switch (s->kind) {
        case jscc::Stmt::Kind::PACKAGE:
            std::cout << "Package(" << s->path << ")\n"; break;
        case jscc::Stmt::Kind::IMPORT:
            std::cout << "Import(" << s->path << ")\n"; break;
        case jscc::Stmt::Kind::VAL:
        case jscc::Stmt::Kind::VAR:
            std::cout << (s->kind == jscc::Stmt::Kind::VAL ? "Val" : "Var")
                      << "(" << s->var_name << ")\n";
            if (s->var_init) print_expr(s->var_init.get(), depth + 1);
            break;
        case jscc::Stmt::Kind::RETURN:
            std::cout << "Return\n";
            if (s->return_val) print_expr(s->return_val->get(), depth + 1);
            break;
        case jscc::Stmt::Kind::DEF:
            std::cout << (s->is_async ? "AsyncDef(" : "Def(")
                      << s->fn_name << ")\n";
            for (auto& p : s->fn_params) {
                print_indent(depth + 1);
                std::cout << "Param(" << p.name << ")\n";
            }
            if (s->fn_expr) print_expr(s->fn_expr.get(), depth + 1);
            for (auto& b : s->fn_body) print_stmt(b.get(), depth + 1);
            break;
        case jscc::Stmt::Kind::DATA_CLASS:
        case jscc::Stmt::Kind::SEALED_CLASS:
        case jscc::Stmt::Kind::CLASS:
            std::cout << "Class(" << s->class_name << ")\n";
            for (auto& b : s->class_body) print_stmt(b.get(), depth + 1);
            break;
        case jscc::Stmt::Kind::EXPR_STMT:
            std::cout << "ExprStmt\n";
            print_expr(s->expr.get(), depth + 1);
            break;
        case jscc::Stmt::Kind::IF:
            std::cout << "If\n";
            print_expr(s->if_cond.get(), depth + 1);
            for (auto& b : s->if_body)   print_stmt(b.get(), depth + 1);
            for (auto& b : s->else_body) print_stmt(b.get(), depth + 1);
            break;
        case jscc::Stmt::Kind::FOR:
            std::cout << "For(" << s->for_var << " in ...)\n";
            for (auto& b : s->for_body) print_stmt(b.get(), depth + 1);
            break;
        case jscc::Stmt::Kind::WHILE:
            std::cout << "While\n";
            for (auto& b : s->while_body) print_stmt(b.get(), depth + 1);
            break;
        default:
            std::cout << "Stmt(?)\n"; break;
    }
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string source;
    std::string filename;

    if (argc >= 2) {
        filename = argv[1];
        std::ifstream f(filename);
        if (!f) {
            std::cerr << "jscc: cannot open file '" << filename << "'\n";
            return 1;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        source = ss.str();
    } else {
        filename = "<test>";
        source   = TEST_SOURCE;
    }

    // ── lex ──────────────────────────────────────────────────────────────
    jscc::Lexer lexer(source, filename);
    auto tokens = lexer.tokenize();
    std::cout << "jscc — lexed " << tokens.size() << " tokens\n\n";

    // ── parse ─────────────────────────────────────────────────────────────
    jscc::ErrorReporter reporter(source, filename);
    jscc::Parser parser(std::move(tokens), reporter);
    auto program = parser.parse();

    if (reporter.has_errors()) {
        std::cerr << "\n" << reporter.error_count() << " error(s). compilation failed.\n";
        return 1;
    }

    // ── print AST ─────────────────────────────────────────────────────────
    std::cout << "AST for package: " << program.package_name << "\n";
    std::cout << std::string(40, '-') << "\n";
    for (auto& stmt : program.stmts)
        print_stmt(stmt.get(), 0);

    std::cout << "\nparse OK — " << program.stmts.size() << " top-level declarations\n";

    // ── import resolution ────────────────────────────────────────────────
    {
        // create a temporary global scope just to run import resolver
        // sema will rebuild its own scope — we share the resolved symbol info
        // via the global scope passed to both
    }

    // ── semantic analysis ─────────────────────────────────────────────────
    jscc::Sema sema(reporter);

    // give sema an import resolver so it can inject package symbols
    jscc::ImportResolver resolver(reporter, sema.global_scope());
    resolver.add_search_path("./jade_modules");
    resolver.add_search_path(std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.jade/packages");
    resolver.resolve(program);

    sema.analyze(program);

    if (reporter.has_errors()) {
        std::cerr << "\n" << reporter.error_count() << " error(s). compilation failed.\n";
        return 1;
    }

    std::cout << "sema OK — no type errors\n";

    // ── parse extra flags ────────────────────────────────────────────────
    std::vector<std::string> extra_targets;
    bool no_bytecode    = false;
    bool bytecode_only  = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.rfind("--target=", 0) == 0) {
            std::string targets = arg.substr(9);
            std::stringstream ss(targets);
            std::string t;
            while (std::getline(ss, t, ',')) extra_targets.push_back(t);
        } else if (arg == "--no-bytecode") {
            no_bytecode = true;
        } else if (arg == "--bytecode-only") {
            bytecode_only = true;
        }
    }

    // ── codegen ───────────────────────────────────────────────────────────
    jscc::Codegen codegen(program.package_name.empty()
                          ? "jade_module" : program.package_name, reporter);

    if (!codegen.generate(program)) {
        std::cerr << "codegen failed\n";
        return 1;
    }

    std::cout << "\n── LLVM IR ──────────────────────────────\n";
    codegen.dump_ir();

    // emit .joc
    std::string out_name = argc >= 3 ? argv[2] : "out.joc";
    if (codegen.emit_joc(out_name, extra_targets, !no_bytecode, bytecode_only)) {
        std::cout << "\ncodegen OK — " << out_name << "\n";
    }

    return 0;
}
