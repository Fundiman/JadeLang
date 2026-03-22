#include "joc_loader.hpp"
#include "hal.hpp"
#include "runtime.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

static void print_usage() {
    std::cout << "usage: jvk <file.joc> [args...]\n"
              << "\n"
              << "options:\n"
              << "  --jit-only       force JIT even if native slice available\n"
              << "  --native-only    fail if no native slice for this arch\n"
              << "  --mem <mb>       heap size in MB (default: 64)\n"
              << "  --info           print .joc info and exit\n"
              << "  --version        print jvk version\n";
}

static void print_version() {
    std::cout << "jvk 0.1.0 — Jade Virtual Kernel\n"
              << "arch: " << jvk::JocLoader::host_arch_tag() << "\n"
              << "platform: ";
    auto* hal = jvk::HAL::create();
    std::cout << hal->platform_name() << "\n";
    delete hal;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    // ── parse flags ───────────────────────────────────────────────────────
    std::string joc_path;
    bool jit_only    = false;
    bool native_only = false;
    bool info_only   = false;
    size_t heap_mb   = 64;
    std::vector<char*> app_args;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if      (arg == "--jit-only")    jit_only    = true;
        else if (arg == "--native-only") native_only = true;
        else if (arg == "--info")        info_only   = true;
        else if (arg == "--version")   { print_version(); return 0; }
        else if (arg == "--mem" && i+1 < argc) {
            heap_mb = std::stoull(argv[++i]);
        }
        else if (arg[0] != '-') {
            if (joc_path.empty()) joc_path = arg;
            else app_args.push_back(argv[i]);
        }
    }

    if (joc_path.empty()) {
        std::cerr << "jvk: no .joc file specified\n";
        return 1;
    }

    // ── load .joc ─────────────────────────────────────────────────────────
    auto joc = jvk::JocLoader::load(joc_path);
    if (!joc) return 1;

    if (info_only) {
        std::cout << "\npackage:  " << joc->package_name << "\n"
                  << "slices:   " << joc->slices.size() << "\n"
                  << "bytecode: " << (joc->has_bytecode() ? "yes" : "no") << "\n"
                  << "arch:     " << jvk::JocLoader::host_arch_tag() << "\n";
        for (auto& s : joc->slices) {
            std::cout << "  [";
            std::cout.write(s.tag, 4);
            std::cout << "] " << s.size << " bytes"
                      << (s.is_bytecode() ? " (LLVM bitcode)" : " (native)")
                      << "\n";
        }
        return 0;
    }

    // ── create HAL + runtime ──────────────────────────────────────────────
    auto* hal = jvk::HAL::create();
    jvk::Runtime runtime(hal);
    runtime.init(".");

    std::cout << "jvk: memory limit " << heap_mb << " MB\n";

    // ── pick slice ────────────────────────────────────────────────────────
    const jvk::JocSlice* slice = nullptr;

    if (jit_only) {
        // force JIT — find LLBC slice
        for (auto& s : joc->slices)
            if (s.is_bytecode()) { slice = &s; break; }
        if (!slice) {
            std::cerr << "jvk: --jit-only but no LLBC slice in " << joc_path << "\n";
            delete hal;
            return 1;
        }
    } else if (native_only) {
        // require native slice
        for (auto& s : joc->slices)
            if (s.is_native() &&
                std::memcmp(s.tag, jvk::JocLoader::host_arch_tag(), 4) == 0) {
                slice = &s;
                break;
            }
        if (!slice) {
            std::cerr << "jvk: --native-only but no '"
                      << jvk::JocLoader::host_arch_tag()
                      << "' slice in " << joc_path << "\n";
            delete hal;
            return 1;
        }
    } else {
        slice = jvk::JocLoader::best_slice(*joc);
    }

    if (!slice) {
        delete hal;
        return 1;
    }

    // ── run ───────────────────────────────────────────────────────────────
    auto [data, size] = joc->slice_data(*slice);

    int app_argc = (int)app_args.size();
    char** app_argv = app_args.data();

    int exit_code = 0;
    if (slice->is_bytecode()) {
        exit_code = runtime.run_jit(data, size, app_argc, app_argv);
    } else {
        exit_code = runtime.run_native(data, size, app_argc, app_argv);
    }

    std::cout << "jvk: exited with code " << exit_code << "\n";
    delete hal;
    return exit_code;
}
