#include "joc_loader.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

namespace jvk {

std::optional<JocFile> JocLoader::load(const std::string& path) {
    // read entire file into memory
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cerr << "jvk: cannot open '" << path << "'\n";
        return std::nullopt;
    }

    size_t size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> raw(size);
    f.read((char*)raw.data(), size);

    if (!validate_header(raw)) return std::nullopt;

    JocFile joc;
    joc.raw = std::move(raw);

    // parse fixed header
    // [0..3]  magic
    // [4]     version
    // [5]     flags
    // [6..7]  reserved
    // [8..11] num_slices
    // [12..15] meta_len

    uint8_t  version    = joc.raw[4];
    uint8_t  flags      = joc.raw[5];
    uint32_t num_slices = 0;
    uint32_t meta_len   = 0;
    std::memcpy(&num_slices, joc.raw.data() + 8,  4);
    std::memcpy(&meta_len,   joc.raw.data() + 12, 4);

    joc.flags = flags;

    // metadata (package name)
    if (16 + meta_len <= joc.raw.size())
        joc.package_name = std::string(
            (char*)joc.raw.data() + 16, meta_len - 1);

    // slice directory
    size_t dir_offset = 16 + meta_len;
    for (uint32_t i = 0; i < num_slices; i++) {
        size_t entry = dir_offset + i * 20;
        if (entry + 20 > joc.raw.size()) break;

        JocSlice s;
        std::memcpy(s.tag,     joc.raw.data() + entry,      4);
        std::memcpy(&s.offset, joc.raw.data() + entry + 4,  8);
        std::memcpy(&s.size,   joc.raw.data() + entry + 12, 8);
        joc.slices.push_back(s);
    }

    std::cout << "jvk: loaded '" << joc.package_name
              << "' — " << joc.slices.size() << " slice(s)\n";
    for (auto& s : joc.slices) {
        std::cout << "  [";
        std::cout.write(s.tag, 4);
        std::cout << "] " << s.size << " bytes\n";
    }

    (void)version; // future use
    return joc;
}

bool JocLoader::validate_header(const std::vector<uint8_t>& raw) {
    if (raw.size() < 16) {
        std::cerr << "jvk: file too small to be a .joc\n";
        return false;
    }
    if (std::memcmp(raw.data(), JOC_MAGIC, 4) != 0) {
        std::cerr << "jvk: not a .joc file (bad magic)\n";
        return false;
    }
    if (raw[4] != JOC_VERSION) {
        std::cerr << "jvk: unsupported .joc version " << (int)raw[4] << "\n";
        return false;
    }
    return true;
}

const char* JocLoader::host_arch_tag() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "ARM_";
#elif defined(__riscv) && __riscv_xlen == 64
    return "RV64";
#elif defined(__wasm__)
    return "WASM";
#else
    return "GEN_";
#endif
}

const JocSlice* JocLoader::best_slice(const JocFile& joc) {
    const char* host = host_arch_tag();
    const JocSlice* bc_slice = nullptr;

    for (auto& s : joc.slices) {
        if (std::memcmp(s.tag, host, 4) == 0)
            return &s;  // perfect native match
        if (s.is_bytecode())
            bc_slice = &s;  // remember bytecode as fallback
    }

    if (bc_slice) {
        std::cout << "jvk: no native slice for '"
                  << host << "' — falling back to LLBC (JIT)\n";
        return bc_slice;
    }

    std::cerr << "jvk: no compatible slice found for arch '" << host << "'\n";
    return nullptr;
}

} // namespace jvk
