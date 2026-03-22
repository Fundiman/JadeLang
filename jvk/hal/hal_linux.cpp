#if defined(__linux__)
#include "hal.hpp"
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <iostream>

namespace jvk {

static int to_mmap_prot(MemProt p) {
    int r = PROT_NONE;
    if ((uint32_t)p & (uint32_t)MemProt::READ)  r |= PROT_READ;
    if ((uint32_t)p & (uint32_t)MemProt::WRITE) r |= PROT_WRITE;
    if ((uint32_t)p & (uint32_t)MemProt::EXEC)  r |= PROT_EXEC;
    return r;
}

class HAL_Linux : public HAL {
public:
    // ── memory ────────────────────────────────────────────────────────────

    void* mem_alloc(size_t size, MemProt prot) override {
        void* p = mmap(nullptr, size, to_mmap_prot(prot),
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return (p == MAP_FAILED) ? nullptr : p;
    }

    void mem_free(void* ptr, size_t size) override {
        munmap(ptr, size);
    }

    bool mem_protect(void* ptr, size_t size, MemProt prot) override {
        return mprotect(ptr, size, to_mmap_prot(prot)) == 0;
    }

    void* mem_map_exec(const void* code, size_t size) override {
        // allocate RW first, copy code, then make RX
        void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return nullptr;
        std::memcpy(p, code, size);
        mprotect(p, size, PROT_READ | PROT_EXEC);
        return p;
    }

    void mem_unmap(void* ptr, size_t size) override {
        munmap(ptr, size);
    }

    // ── threads ───────────────────────────────────────────────────────────

    ThreadId thread_create(ThreadFn fn) override {
        auto* ctx = new ThreadFn(std::move(fn));
        pthread_t tid;
        pthread_create(&tid, nullptr, [](void* arg) -> void* {
            auto* f = (ThreadFn*)arg;
            (*f)();
            delete f;
            return nullptr;
        }, ctx);
        pthread_detach(tid);
        return (ThreadId)tid;
    }

    void thread_join(ThreadId id) override {
        pthread_join((pthread_t)id, nullptr);
    }

    void thread_yield() override {
        sched_yield();
    }

    ThreadId thread_current_id() override {
        return (ThreadId)pthread_self();
    }

    // ── file I/O ──────────────────────────────────────────────────────────

    int file_open(const std::string& path, int flags) override {
        return ::open(path.c_str(), flags, 0644);
    }

    void file_close(int fd) override {
        ::close(fd);
    }

    ssize_t file_read(int fd, void* buf, size_t n) override {
        return ::read(fd, buf, n);
    }

    ssize_t file_write(int fd, const void* buf, size_t n) override {
        return ::write(fd, buf, n);
    }

    bool file_exists(const std::string& path) override {
        struct stat st;
        return stat(path.c_str(), &st) == 0;
    }

    // ── time ──────────────────────────────────────────────────────────────

    uint64_t time_now_ms() override {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }

    void time_sleep_ms(uint64_t ms) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    // ── process ───────────────────────────────────────────────────────────

    void proc_exit(int code) override {
        ::exit(code);
    }

    std::string env_get(const std::string& key) override {
        const char* v = ::getenv(key.c_str());
        return v ? v : "";
    }

    std::string platform_name() override {
        return "linux";
    }
};

HAL* HAL::create() { return new HAL_Linux(); }

} // namespace jvk
#endif
