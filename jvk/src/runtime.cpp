#include "runtime.hpp"

#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/RTDyldMemoryManager.h>
#include <llvm/ExecutionEngine/RuntimeDyld.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#include <iostream>
#include <algorithm>
#include <cstring>

namespace jvk {

// ── Scheduler ─────────────────────────────────────────────────────────────────

Scheduler::Scheduler(HAL* hal) : m_hal(hal) {}

uint64_t Scheduler::spawn(const std::string& name, ThreadFn fn) {
    uint64_t id = m_next_id++;
    GreenThread gt{ id, name, fn, false };
    m_threads.push_back(gt);

    // launch as OS thread via HAL
    ThreadId os_id = m_hal->thread_create(std::move(fn));
    m_os_threads[id] = os_id;
    return id;
}

void Scheduler::yield() {
    m_hal->thread_yield();
}

void Scheduler::join(uint64_t id) {
    auto it = m_os_threads.find(id);
    if (it != m_os_threads.end())
        m_hal->thread_join(it->second);
}

void Scheduler::run_all() {
    for (auto& [id, _] : m_os_threads)
        m_hal->thread_join(m_os_threads[id]);
}

// ── MemoryManager ─────────────────────────────────────────────────────────────

MemoryManager::MemoryManager(HAL* hal, size_t heap_size)
    : m_hal(hal), m_heap_size(heap_size) {}

MemoryManager::~MemoryManager() {}

void* MemoryManager::alloc(size_t size) {
    // for now delegate to HAL mmap
    // a real slab/bump allocator would go here
    m_used += size;
    return m_hal->mem_alloc(size, MemProt::RW);
}

void MemoryManager::free(void* ptr) {
    // placeholder — real RC/GC goes here
    (void)ptr;
}

void* MemoryManager::alloc_exec(size_t size) {
    return m_hal->mem_alloc(size, MemProt::RWX);
}

void MemoryManager::free_exec(void* ptr, size_t size) {
    m_hal->mem_free(ptr, size);
}

// ── VFS ───────────────────────────────────────────────────────────────────────

VFS::VFS(HAL* hal) : m_hal(hal) {
    // default: stdin/stdout/stderr pass through
}

void VFS::mount(const std::string& virt_path, const std::string& host_path) {
    m_mounts[virt_path] = host_path;
    std::cout << "jvk: vfs mount " << virt_path
              << " -> " << host_path << "\n";
}

std::string VFS::resolve(const std::string& virt_path) const {
    // find longest matching mount prefix
    std::string best_virt;
    for (auto& [virt, host] : m_mounts) {
        if (virt_path.find(virt) == 0 && virt.size() > best_virt.size())
            best_virt = virt;
    }
    if (best_virt.empty()) return virt_path; // passthrough
    auto it = m_mounts.find(best_virt);
    return it->second + virt_path.substr(best_virt.size());
}

int VFS::open(const std::string& virt_path, int flags) {
    auto host = resolve(virt_path);
    int host_fd = m_hal->file_open(host, flags);
    if (host_fd < 0) return -1;
    int vfd = m_next_fd++;
    m_fd_map[vfd] = host_fd;
    return vfd;
}

void VFS::close(int fd) {
    auto it = m_fd_map.find(fd);
    if (it != m_fd_map.end()) {
        m_hal->file_close(it->second);
        m_fd_map.erase(it);
    }
}

ssize_t VFS::read(int fd, void* buf, size_t n) {
    auto it = m_fd_map.find(fd);
    if (it == m_fd_map.end()) return -1;
    return m_hal->file_read(it->second, buf, n);
}

ssize_t VFS::write(int fd, const void* buf, size_t n) {
    auto it = m_fd_map.find(fd);
    if (it == m_fd_map.end()) return -1;
    return m_hal->file_write(it->second, buf, n);
}

bool VFS::exists(const std::string& virt_path) {
    return m_hal->file_exists(resolve(virt_path));
}

// ── Runtime ───────────────────────────────────────────────────────────────────

Runtime::Runtime(HAL* hal) : m_hal(hal) {
    m_scheduler = std::make_unique<Scheduler>(hal);
    m_memory    = std::make_unique<MemoryManager>(hal);
    m_vfs       = std::make_unique<VFS>(hal);
}

Runtime::~Runtime() {}

void Runtime::init(const std::string& app_root) {
    // mount app directory as "/"
    m_vfs->mount("/", app_root);
    std::cout << "jvk: runtime initialized on "
              << m_hal->platform_name() << "\n";
}

// ── native execution ──────────────────────────────────────────────────────────
// mmaps the native code slice as executable and jumps to its entry point

int Runtime::run_native(const uint8_t* code, size_t size,
                         int argc, char** argv) {
    std::cout << "jvk: loading native slice via RuntimeDyld ("
              << size << " bytes)\n";

    // copy slice into an aligned owned buffer
    // (raw .joc data may be unaligned — MemoryBuffer needs alignment)
    auto buf = llvm::MemoryBuffer::getMemBufferCopy(
        llvm::StringRef((const char*)code, size), "native_slice");

    // parse as an object file
    auto obj_or_err = llvm::object::ObjectFile::createObjectFile(*buf);
    if (!obj_or_err) {
        std::string err;
        llvm::raw_string_ostream es(err);
        es << obj_or_err.takeError();
        std::cerr << "jvk: failed to parse object: " << err << "\n";
        return 1;
    }

    // custom memory manager that resolves host symbols (printf etc)
    struct JvkMemMgr : public llvm::SectionMemoryManager {
        uint64_t getSymbolAddress(const std::string& name) override {
            // try host process first
#if !defined(_WIN32)
            void* sym = dlsym(RTLD_DEFAULT, name.c_str());
            if (sym) return (uint64_t)(uintptr_t)sym;
#endif
            return llvm::SectionMemoryManager::getSymbolAddress(name);
        }
    };

    auto mem_mgr = std::make_shared<JvkMemMgr>();
    llvm::RuntimeDyld dyld(*mem_mgr, *mem_mgr);

    // load the object
    auto info = dyld.loadObject(*obj_or_err->get());
    if (!info) {
        std::cerr << "jvk: RuntimeDyld failed to load object\n";
        return 1;
    }

    dyld.resolveRelocations();
    mem_mgr->finalizeMemory();

    if (dyld.hasError()) {
        std::cerr << "jvk: dyld error: " << dyld.getErrorString().str() << "\n";
        return 1;
    }

    auto sym = dyld.getSymbol("main");
    if (!sym.getAddress()) {
        std::cerr << "jvk: no 'main' symbol in native slice\n";
        return 1;
    }

    using MainFn = int(*)(int, char**);
    auto* entry = (MainFn)(uintptr_t)sym.getAddress();

    std::cout << "jvk: jumping to native main @ "
              << (void*)entry << "\n";
    return entry(argc, argv);
}

// ── JIT execution ─────────────────────────────────────────────────────────────
// uses LLVM LLJIT to compile and run LLVM bitcode on the fly

int Runtime::run_jit(const uint8_t* bc, size_t size,
                      int argc, char** argv) {
    std::cout << "jvk: JIT compiling LLBC slice via MCJIT ("
              << size << " bytes)\n";

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // parse the LLVM bitcode into a Module
    auto buf = llvm::MemoryBuffer::getMemBuffer(
        llvm::StringRef((const char*)bc, size), "", false);

    auto ctx = std::make_unique<llvm::LLVMContext>();
    auto mod_or_err = llvm::parseBitcodeFile(*buf, *ctx);
    if (!mod_or_err) {
        std::string err;
        llvm::raw_string_ostream es(err);
        es << mod_or_err.takeError();
        std::cerr << "jvk: failed to parse bitcode: " << err << "\n";
        return 1;
    }

    auto module = std::move(*mod_or_err);
    std::cout << "jvk: parsed module '"
              << module->getName().str() << "'\n";

    // build MCJIT execution engine
    std::string err_str;
    llvm::EngineBuilder builder(std::move(module));
    builder.setErrorStr(&err_str);
    builder.setEngineKind(llvm::EngineKind::JIT);
    builder.setOptLevel(llvm::CodeGenOptLevel::Default);

    auto* engine = builder.create();
    if (!engine) {
        std::cerr << "jvk: failed to create JIT engine: " << err_str << "\n";
        return 1;
    }

    // compile everything
    engine->finalizeObject();

    // look up main
    auto main_addr = engine->getFunctionAddress("main");
    if (!main_addr) {
        std::cerr << "jvk: no 'main' in JIT module\n";
        delete engine;
        return 1;
    }

    using MainFn = int(*)(int, char**);
    auto* entry = (MainFn)main_addr;

    std::cout << "jvk: JIT compiled — jumping to main @ "
              << (void*)entry << "\n";
    int result = entry(argc, argv);
    delete engine;
    return result;
}

} // namespace jvk
