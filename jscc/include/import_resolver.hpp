#pragma once
#include "ast.hpp"
#include "error.hpp"
#include "sema.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

namespace jscc {

namespace fs = std::filesystem;

// ── package descriptor ────────────────────────────────────────────────────────
// represents one resolved package — its path on disk and its exported symbols

struct PackageInfo {
    std::string              package_name;   // "jade.stdlib.io"
    fs::path                 source_path;    // /usr/local/jade/stdlib/io.jsc
    std::vector<std::string> exports;        // ["println", "print", "readln"]
    bool                     is_builtin = false;
};

// ── import resolver ───────────────────────────────────────────────────────────

class ImportResolver {
public:
    ImportResolver(ErrorReporter& reporter, Scope* global_scope);

    // add a search root (e.g. ~/.jade/packages, ./jade_modules, stdlib dir)
    void add_search_path(const fs::path& path);

    // resolve all imports in a program, inject symbols into global scope
    void resolve(Program& program);

    // register a builtin package directly (for jade.stdlib.*)
    void register_builtin(const std::string& package_name,
                          const std::vector<std::string>& exports,
                          const TypeRef& module_type = nullptr);

private:
    ErrorReporter&  m_reporter;
    Scope*          m_global_scope;
    std::vector<fs::path> m_search_paths;

    // cache: package name -> info
    std::unordered_map<std::string, PackageInfo> m_package_cache;

    // already resolved (avoid double-resolving)
    std::unordered_set<std::string> m_resolved;

    // builtin package symbol tables
    // package_name -> { symbol_name -> type }
    std::unordered_map<std::string,
        std::unordered_map<std::string, TypeRef>> m_builtins;

    void        resolve_import(Stmt* import_stmt);
    void        inject_package(const std::string& package_name,
                               const std::vector<std::string>& specific_imports);
    fs::path    find_package(const std::string& package_name);
    std::string package_to_path(const std::string& package_name);
    std::string suggest_similar(const std::string& package_name);
    void        register_all_builtins();
};

} // namespace jscc
