#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <functional>

namespace jvk {

// ── memory protection flags ───────────────────────────────────────────────────

enum class MemProt : uint32_t {
    NONE  = 0,
    READ  = 1 << 0,
    WRITE = 1 << 1,
    EXEC  = 1 << 2,
    RW    = READ | WRITE,
    RX    = READ | EXEC,
    RWX   = READ | WRITE | EXEC,
};

inline MemProt operator|(MemProt a, MemProt b) {
    return (MemProt)((uint32_t)a | (uint32_t)b);
}
inline bool operator&(MemProt a, MemProt b) {
    return ((uint32_t)a & (uint32_t)b) != 0;
}

// ── thread handle ─────────────────────────────────────────────────────────────

using ThreadFn  = std::function<void()>;
using ThreadId  = uint64_t;

// ── HAL interface ─────────────────────────────────────────────────────────────
// one implementation per platform: hal_linux.cpp / hal_macos.cpp / hal_windows.cpp

class HAL {
public:
    virtual ~HAL() = default;

    // ── memory ────────────────────────────────────────────────────────────
    virtual void*  mem_alloc(size_t size, MemProt prot)          = 0;
    virtual void   mem_free(void* ptr, size_t size)              = 0;
    virtual bool   mem_protect(void* ptr, size_t size, MemProt)  = 0;

    // map file into memory (for native slice execution)
    virtual void*  mem_map_exec(const void* code, size_t size)   = 0;
    virtual void   mem_unmap(void* ptr, size_t size)             = 0;

    // ── threads ───────────────────────────────────────────────────────────
    virtual ThreadId thread_create(ThreadFn fn)                  = 0;
    virtual void     thread_join(ThreadId id)                    = 0;
    virtual void     thread_yield()                              = 0;
    virtual ThreadId thread_current_id()                         = 0;

    // ── file I/O ──────────────────────────────────────────────────────────
    virtual int    file_open(const std::string& path, int flags) = 0;
    virtual void   file_close(int fd)                            = 0;
    virtual ssize_t file_read(int fd, void* buf, size_t n)       = 0;
    virtual ssize_t file_write(int fd, const void* buf, size_t n)= 0;
    virtual bool   file_exists(const std::string& path)          = 0;

    // ── time ──────────────────────────────────────────────────────────────
    virtual uint64_t time_now_ms()                               = 0;
    virtual void     time_sleep_ms(uint64_t ms)                  = 0;

    // ── process ───────────────────────────────────────────────────────────
    virtual void   proc_exit(int code)                           = 0;
    virtual std::string env_get(const std::string& key)          = 0;
    virtual std::string platform_name()                          = 0;

    // ── factory ───────────────────────────────────────────────────────────
    static HAL* create();   // returns the right impl for current platform
};

} // namespace jvk
