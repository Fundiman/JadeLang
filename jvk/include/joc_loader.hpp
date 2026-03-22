#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace jvk {

// ── .joc format constants ─────────────────────────────────────────────────────

static constexpr char     JOC_MAGIC[4]   = {'J','A','D','E'};
static constexpr uint8_t  JOC_VERSION    = 1;
static constexpr uint8_t  FLAG_HAS_BC    = 0x01;
static constexpr uint8_t  FLAG_BC_ONLY   = 0x02;

// arch tags
static constexpr char TAG_X86[4]  = {'x','8','6','_'};
static constexpr char TAG_ARM[4]  = {'A','R','M','_'};
static constexpr char TAG_RV64[4] = {'R','V','6','4'};
static constexpr char TAG_WASM[4] = {'W','A','S','M'};
static constexpr char TAG_LLBC[4] = {'L','L','B','C'};

// ── slice descriptor ──────────────────────────────────────────────────────────

struct JocSlice {
    char     tag[4];
    uint64_t offset;
    uint64_t size;
    bool     is_bytecode() const {
        return tag[0]=='L' && tag[1]=='L' && tag[2]=='B' && tag[3]=='C';
    }
    bool     is_native()   const { return !is_bytecode(); }
};

// ── loaded .joc file ──────────────────────────────────────────────────────────

struct JocFile {
    std::string              package_name;
    uint8_t                  flags        = 0;
    std::vector<JocSlice>    slices;
    std::vector<uint8_t>     raw;          // entire file in memory

    bool has_bytecode() const { return flags & FLAG_HAS_BC; }
    bool bytecode_only() const { return flags & FLAG_BC_ONLY; }

    // returns pointer + size into raw buffer for a slice
    std::pair<const uint8_t*, size_t> slice_data(const JocSlice& s) const {
        return { raw.data() + s.offset, s.size };
    }
};

// ── loader ────────────────────────────────────────────────────────────────────

class JocLoader {
public:
    // load a .joc file from disk into memory
    static std::optional<JocFile> load(const std::string& path);

    // pick the best slice for the current CPU
    // prefers native, falls back to LLBC
    static const JocSlice* best_slice(const JocFile& joc);

    // return the host arch tag (e.g. "x86_")
    static const char* host_arch_tag();

private:
    static bool validate_header(const std::vector<uint8_t>& raw);
};

} // namespace jvk
