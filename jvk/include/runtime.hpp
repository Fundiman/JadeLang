#pragma once
#include "hal.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace jvk {

// ── green thread ──────────────────────────────────────────────────────────────

struct GreenThread {
    uint64_t    id;
    std::string name;
    ThreadFn    fn;
    bool        finished = false;
};

// ── scheduler ────────────────────────────────────────────────────────────────

class Scheduler {
public:
    explicit Scheduler(HAL* hal);

    uint64_t spawn(const std::string& name, ThreadFn fn);
    void     yield();
    void     join(uint64_t id);
    void     run_all();

private:
    HAL*                                   m_hal;
    std::vector<GreenThread>               m_threads;
    std::unordered_map<uint64_t, ThreadId> m_os_threads;
    uint64_t                               m_next_id = 1;
};

// ── memory manager ────────────────────────────────────────────────────────────

class MemoryManager {
public:
    explicit MemoryManager(HAL* hal, size_t heap_size = 64 * 1024 * 1024);
    ~MemoryManager();

    void*  alloc(size_t size);
    void   free(void* ptr);
    void*  alloc_exec(size_t size);   // for JIT output
    void   free_exec(void* ptr, size_t size);

    size_t used()  const { return m_used; }
    size_t total() const { return m_heap_size; }

private:
    HAL*    m_hal;
    size_t  m_heap_size;
    size_t  m_used = 0;
};

// ── VFS node ──────────────────────────────────────────────────────────────────

struct VfsNode {
    enum class Kind { FILE, DIR, DEVICE };
    Kind        kind;
    std::string name;
    std::string host_path;  // mapped to real path on disk, empty = virtual
    std::vector<std::string> children;
};

// ── virtual filesystem ────────────────────────────────────────────────────────

class VFS {
public:
    explicit VFS(HAL* hal);

    // mount a host path at a virtual path
    // e.g. mount("/", "/home/user/myapp") — app thinks it owns "/"
    void mount(const std::string& virt_path, const std::string& host_path);

    // standard file ops — apps call these, VFS translates to host paths
    int     open(const std::string& virt_path, int flags);
    void    close(int fd);
    ssize_t read(int fd, void* buf, size_t n);
    ssize_t write(int fd, const void* buf, size_t n);
    bool    exists(const std::string& virt_path);

    // resolve virtual path to host path
    std::string resolve(const std::string& virt_path) const;

private:
    HAL*    m_hal;
    std::unordered_map<std::string, std::string> m_mounts;
    std::unordered_map<int, int>                 m_fd_map;   // virt fd -> host fd
    int m_next_fd = 3; // 0=stdin 1=stdout 2=stderr
};

// ── runtime ───────────────────────────────────────────────────────────────────
// the whole kernel — owns HAL, scheduler, memory, VFS

class Runtime {
public:
    explicit Runtime(HAL* hal);
    ~Runtime();

    // initialize — call before running any .joc
    void init(const std::string& app_root = ".");

    // run a native code slice — mmaps it executable and calls entry
    int run_native(const uint8_t* code, size_t size,
                   int argc, char** argv);

    // run LLVM bitcode slice via LLJIT
    int run_jit(const uint8_t* bc, size_t size,
                int argc, char** argv);

    HAL*           hal()       { return m_hal; }
    Scheduler*     scheduler() { return m_scheduler.get(); }
    MemoryManager* memory()    { return m_memory.get(); }
    VFS*           vfs()       { return m_vfs.get(); }

private:
    HAL*                          m_hal;
    std::unique_ptr<Scheduler>    m_scheduler;
    std::unique_ptr<MemoryManager> m_memory;
    std::unique_ptr<VFS>          m_vfs;
};

} // namespace jvk
