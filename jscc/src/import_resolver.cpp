#include "import_resolver.hpp"
#include <iostream>
#include <sstream>

namespace jscc {

ImportResolver::ImportResolver(ErrorReporter& reporter, Scope* global_scope)
    : m_reporter(reporter), m_global_scope(global_scope) {
    register_all_builtins();
}

void ImportResolver::add_search_path(const fs::path& path) {
    m_search_paths.push_back(path);
}

// ── builtin registry ──────────────────────────────────────────────────────────
// defines every jade.stdlib.* package and what symbols it exports

void ImportResolver::register_all_builtins() {

    // jade.stdlib.io
    m_builtins["jade.stdlib.io"] = {
        { "println",  JadeType::make("void") },
        { "print",    JadeType::make("void") },
        { "readln",   JadeType::make("str")  },
        { "readInt",  JadeType::make("i32")  },
        { "stderr",   JadeType::make("void") },
        { "flush",    JadeType::make("void") },
    };

    // jade.stdlib.math
    m_builtins["jade.stdlib.math"] = {
        { "sqrt",     JadeType::make("f64")  },
        { "abs",      JadeType::make("f64")  },
        { "pow",      JadeType::make("f64")  },
        { "floor",    JadeType::make("f64")  },
        { "ceil",     JadeType::make("f64")  },
        { "round",    JadeType::make("f64")  },
        { "sin",      JadeType::make("f64")  },
        { "cos",      JadeType::make("f64")  },
        { "tan",      JadeType::make("f64")  },
        { "log",      JadeType::make("f64")  },
        { "log2",     JadeType::make("f64")  },
        { "log10",    JadeType::make("f64")  },
        { "min",      JadeType::make("f64")  },
        { "max",      JadeType::make("f64")  },
        { "PI",       JadeType::make("f64")  },
        { "E",        JadeType::make("f64")  },
        { "INF",      JadeType::make("f64")  },
    };

    // jade.stdlib.net.http
    m_builtins["jade.stdlib.net.http"] = {
        { "get",      JadeType::make("Result") },
        { "post",     JadeType::make("Result") },
        { "put",      JadeType::make("Result") },
        { "delete_",  JadeType::make("Result") },
        { "patch",    JadeType::make("Result") },
    };

    // jade.stdlib.net
    m_builtins["jade.stdlib.net"] = {
        { "http",     JadeType::make("Module") },
        { "tcp",      JadeType::make("Module") },
        { "udp",      JadeType::make("Module") },
    };

    // jade.stdlib.collections
    m_builtins["jade.stdlib.collections"] = {
        { "List",     JadeType::make("class") },
        { "Map",      JadeType::make("class") },
        { "Set",      JadeType::make("class") },
        { "Queue",    JadeType::make("class") },
        { "Stack",    JadeType::make("class") },
    };

    // jade.stdlib.fs  (file system)
    m_builtins["jade.stdlib.fs"] = {
        { "read",     JadeType::make("Result") },
        { "write",    JadeType::make("Result") },
        { "exists",   JadeType::make("bool")   },
        { "delete_",  JadeType::make("Result") },
        { "mkdir",    JadeType::make("Result") },
        { "ls",       JadeType::make("Result") },
    };

    // jade.stdlib.os
    m_builtins["jade.stdlib.os"] = {
        { "env",      JadeType::make("str")    },
        { "args",     JadeType::make("[]str")  },
        { "exit",     JadeType::make("void")   },
        { "platform", JadeType::make("str")    },
        { "arch",     JadeType::make("str")    },
    };

    // jade.stdlib.time
    m_builtins["jade.stdlib.time"] = {
        { "now",      JadeType::make("i64")    },
        { "sleep",    JadeType::make("void")   },
        { "format",   JadeType::make("str")    },
    };

    // jade.stdlib.json
    m_builtins["jade.stdlib.json"] = {
        { "parse",    JadeType::make("Result") },
        { "stringify",JadeType::make("str")    },
    };

    // jade.stdlib.crypto
    m_builtins["jade.stdlib.crypto"] = {
        { "sha256",   JadeType::make("str")    },
        { "md5",      JadeType::make("str")    },
        { "uuid",     JadeType::make("str")    },
        { "random",   JadeType::make("u64")    },
    };

    // jade.stdlib.web  (web framework)
    m_builtins["jade.stdlib.web"] = {
        { "WebApp",   JadeType::make("class")  },
        { "Request",  JadeType::make("class")  },
        { "Response", JadeType::make("class")  },
        { "Router",   JadeType::make("class")  },
    };
}

// ── main resolve pass ─────────────────────────────────────────────────────────

void ImportResolver::resolve(Program& program) {
    for (auto& stmt : program.stmts) {
        if (stmt->kind == Stmt::Kind::IMPORT)
            resolve_import(stmt.get());
    }
}

void ImportResolver::resolve_import(Stmt* stmt) {
    const auto& pkg = stmt->path;

    // skip if already resolved
    if (m_resolved.count(pkg)) return;
    m_resolved.insert(pkg);

    // check builtins first
    auto it = m_builtins.find(pkg);
    if (it != m_builtins.end()) {
        inject_package(pkg, stmt->imports);
        return;
    }

    // try to find on disk
    auto path = find_package(pkg);
    if (path.empty()) {
        // try partial match — maybe they imported jade.stdlib.net
        // and we have jade.stdlib.net.http — inject the parent namespace
        bool found_child = false;
        for (auto& [builtin_name, _] : m_builtins) {
            if (builtin_name.find(pkg) == 0) {
                found_child = true;
                break;
            }
        }

        if (!found_child) {
            m_reporter.error("JD401",
                "package '" + pkg + "' not found",
                { m_reporter.filename(), stmt->line, stmt->col,
                  stmt->col + (int)pkg.size() },
                suggest_similar(pkg));
        }
        return;
    }

    inject_package(pkg, stmt->imports);
}

// ── symbol injection ──────────────────────────────────────────────────────────
// injects symbols from a package into the global scope
// if specific_imports is non-empty, only inject those
// otherwise inject the whole package as a module object

void ImportResolver::inject_package(const std::string& pkg,
                                     const std::vector<std::string>& specific) {
    auto it = m_builtins.find(pkg);
    if (it == m_builtins.end()) return;

    auto& symbols = it->second;

    if (!specific.empty()) {
        // import jade.stdlib.collections.{ List, Map }
        // inject List and Map directly into global scope
        for (auto& name : specific) {
            auto sit = symbols.find(name);
            if (sit == symbols.end()) {
                m_reporter.error("JD402",
                    "'" + name + "' is not exported by '" + pkg + "'",
                    { m_reporter.filename(), 0, 0, 0 });
                continue;
            }
            m_global_scope->define(name, {
                Symbol::Kind::VAL, name, sit->second, false, 0, 0
            });
        }
    } else {
        // import jade.stdlib.io
        // inject as module: io.println(...) etc.
        // extract the last segment as the module name
        std::string module_name = pkg;
        auto dot = pkg.rfind('.');
        if (dot != std::string::npos)
            module_name = pkg.substr(dot + 1);

        // create a module type and inject it
        auto module_type  = std::make_shared<JadeType>();
        module_type->kind = JadeType::Kind::CLASS;
        module_type->name = "Module<" + pkg + ">";

        // also inject each exported symbol under the module
        // so both io.println() and (after using) println() work
        for (auto& [sym_name, sym_type] : symbols) {
            // module_name.sym_name accessible via method call — handled at runtime
            // for now just register the module itself
            (void)sym_name; (void)sym_type;
        }

        m_global_scope->define(module_name, {
            Symbol::Kind::VAL, module_name, module_type, false, 0, 0
        });

        // also register all exported symbols as resolvable
        // (needed for when they call io.println — sema sees 'io' as defined)
        for (auto& [sym_name, sym_type] : symbols) {
            // register as module.sym in a virtual namespace
            // full qualified access checked at method-call resolution
            (void)sym_name; (void)sym_type;
        }
    }
}

// ── disk search ───────────────────────────────────────────────────────────────

std::string ImportResolver::package_to_path(const std::string& pkg) {
    // jade.stdlib.io -> jade/stdlib/io.jsc
    std::string path = pkg;
    for (auto& c : path)
        if (c == '.') c = '/';
    return path + ".jsc";
}

fs::path ImportResolver::find_package(const std::string& pkg) {
    auto rel = package_to_path(pkg);
    for (auto& root : m_search_paths) {
        auto candidate = root / rel;
        if (fs::exists(candidate))
            return candidate;
    }
    return {};
}

// ── suggestion helper ─────────────────────────────────────────────────────────
// "did you mean jade.stdlib.io?" style hints

std::string ImportResolver::suggest_similar(const std::string& pkg) {
    // find closest builtin name
    size_t best_match = 0;
    std::string best_name;

    for (auto& [name, _] : m_builtins) {
        // count common prefix length
        size_t match = 0;
        while (match < pkg.size() && match < name.size() &&
               pkg[match] == name[match]) match++;
        if (match > best_match) {
            best_match = match;
            best_name  = name;
        }
    }

    if (!best_name.empty() && best_match >= 4)
        return "did you mean '" + best_name + "'?";

    return "check your jade.toml dependencies";
}

void ImportResolver::register_builtin(const std::string& pkg,
                                       const std::vector<std::string>& exports,
                                       const TypeRef& module_type) {
    std::unordered_map<std::string, TypeRef> syms;
    for (auto& e : exports)
        syms[e] = module_type ? module_type : JadeType::unknown();
    m_builtins[pkg] = std::move(syms);
}

} // namespace jscc
