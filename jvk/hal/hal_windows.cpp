#if defined(_WIN32)
#include "hal.hpp"
#include <windows.h>
#include <cstring>
#include <chrono>
#include <thread>

namespace jvk {

static DWORD to_win_prot(MemProt p) {
    bool r = (uint32_t)p & (uint32_t)MemProt::READ;
    bool w = (uint32_t)p & (uint32_t)MemProt::WRITE;
    bool x = (uint32_t)p & (uint32_t)MemProt::EXEC;
    if (x && w) return PAGE_EXECUTE_READWRITE;
    if (x && r) return PAGE_EXECUTE_READ;
    if (x)      return PAGE_EXECUTE;
    if (w)      return PAGE_READWRITE;
    if (r)      return PAGE_READONLY;
    return PAGE_NOACCESS;
}

class HAL_Windows : public HAL {
public:
    // ── memory ────────────────────────────────────────────────────────────

    void* mem_alloc(size_t size, MemProt prot) override {
        return VirtualAlloc(nullptr, size,
                            MEM_COMMIT | MEM_RESERVE, to_win_prot(prot));
    }

    void mem_free(void* ptr, size_t) override {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }

    bool mem_protect(void* ptr, size_t size, MemProt prot) override {
        DWORD old;
        return VirtualProtect(ptr, size, to_win_prot(prot), &old) != 0;
    }

    void* mem_map_exec(const void* code, size_t size) override {
        void* p = VirtualAlloc(nullptr, size,
                               MEM_COMMIT | MEM_RESERVE,
                               PAGE_EXECUTE_READWRITE);
        if (!p) return nullptr;
        std::memcpy(p, code, size);
        DWORD old;
        VirtualProtect(p, size, PAGE_EXECUTE_READ, &old);
        return p;
    }

    void mem_unmap(void* ptr, size_t) override {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }

    // ── threads ───────────────────────────────────────────────────────────

    ThreadId thread_create(ThreadFn fn) override {
        auto* ctx = new ThreadFn(std::move(fn));
        HANDLE h = CreateThread(nullptr, 0, [](LPVOID arg) -> DWORD {
            auto* f = (ThreadFn*)arg;
            (*f)();
            delete f;
            return 0;
        }, ctx, 0, nullptr);
        return (ThreadId)(uintptr_t)h;
    }

    void thread_join(ThreadId id) override {
        HANDLE h = (HANDLE)(uintptr_t)id;
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
    }

    void thread_yield() override {
        SwitchToThread();
    }

    ThreadId thread_current_id() override {
        return (ThreadId)GetCurrentThreadId();
    }

    // ── file I/O ──────────────────────────────────────────────────────────

    int file_open(const std::string& path, int flags) override {
        DWORD access  = GENERIC_READ;
        DWORD create  = OPEN_EXISTING;
        if (flags & 1) { access |= GENERIC_WRITE; create = OPEN_ALWAYS; }
        HANDLE h = CreateFileA(path.c_str(), access, FILE_SHARE_READ,
                               nullptr, create, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return -1;
        return (int)(intptr_t)h;
    }

    void file_close(int fd) override {
        CloseHandle((HANDLE)(intptr_t)fd);
    }

    ssize_t file_read(int fd, void* buf, size_t n) override {
        DWORD read = 0;
        ReadFile((HANDLE)(intptr_t)fd, buf, (DWORD)n, &read, nullptr);
        return (ssize_t)read;
    }

    ssize_t file_write(int fd, const void* buf, size_t n) override {
        DWORD written = 0;
        WriteFile((HANDLE)(intptr_t)fd, buf, (DWORD)n, &written, nullptr);
        return (ssize_t)written;
    }

    bool file_exists(const std::string& path) override {
        return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // ── time ──────────────────────────────────────────────────────────────

    uint64_t time_now_ms() override {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }

    void time_sleep_ms(uint64_t ms) override {
        Sleep((DWORD)ms);
    }

    // ── process ───────────────────────────────────────────────────────────

    void proc_exit(int code) override {
        ExitProcess(code);
    }

    std::string env_get(const std::string& key) override {
        char buf[32767];
        DWORD n = GetEnvironmentVariableA(key.c_str(), buf, sizeof(buf));
        return n ? std::string(buf, n) : "";
    }

    std::string platform_name() override {
        return "windows";
    }
};

HAL* HAL::create() { return new HAL_Windows(); }

} // namespace jvk
#endif
