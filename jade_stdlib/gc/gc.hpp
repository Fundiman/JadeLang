#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <vector>
#include <unordered_set>
#include <functional>
#include <mutex>

namespace jade {

// ── GC object header ──────────────────────────────────────────────────────────
// every heap-allocated Jade object starts with this header

struct GcHeader {
    std::atomic<int32_t> rc;        // reference count
    uint8_t              color;     // cycle detector color
    uint8_t              buffered;  // in cycle detector buffer?
    uint16_t             type_id;   // object type tag

    // function pointers set by each object type
    void (*trace)(GcHeader*, std::function<void(GcHeader*)>);  // visit children
    void (*destroy)(GcHeader*);                                 // destructor

    GcHeader(uint16_t tid, 
             void(*tr)(GcHeader*, std::function<void(GcHeader*)>),
             void(*dest)(GcHeader*))
        : rc(1), color(0), buffered(0), type_id(tid)
        , trace(tr), destroy(dest) {}
};

// ── cycle detector colors (Bacon-Rajan algorithm) ─────────────────────────────
namespace color {
    static constexpr uint8_t BLACK  = 0;  // in use, rc > 0
    static constexpr uint8_t GRAY   = 1;  // possible cycle member
    static constexpr uint8_t WHITE  = 2;  // garbage (confirmed dead)
    static constexpr uint8_t PURPLE = 3;  // candidate root
    static constexpr uint8_t GREEN  = 4;  // acyclic (never enters cycle buf)
}

// ── RC smart pointer ──────────────────────────────────────────────────────────

template<typename T>
class Rc {
public:
    Rc() : m_ptr(nullptr) {}

    explicit Rc(T* ptr) : m_ptr(ptr) {
        // rc starts at 1 in GcHeader ctor — no increment needed
    }

    Rc(const Rc& other) : m_ptr(other.m_ptr) {
        if (m_ptr) inc_ref(header());
    }

    Rc(Rc&& other) noexcept : m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
    }

    ~Rc() {
        if (m_ptr) dec_ref(header());
    }

    Rc& operator=(const Rc& other) {
        if (this != &other) {
            if (m_ptr) dec_ref(header());
            m_ptr = other.m_ptr;
            if (m_ptr) inc_ref(header());
        }
        return *this;
    }

    Rc& operator=(Rc&& other) noexcept {
        if (this != &other) {
            if (m_ptr) dec_ref(header());
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    T* get()       const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    T& operator*()  const { return *m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

    int32_t ref_count() const {
        return m_ptr ? header()->rc.load() : 0;
    }

private:
    T*         m_ptr;
    GcHeader*  header() const {
        // GcHeader is always at the start of the allocation
        return reinterpret_cast<GcHeader*>(
            reinterpret_cast<uint8_t*>(m_ptr) - sizeof(GcHeader));
    }

    static void inc_ref(GcHeader* h);
    static void dec_ref(GcHeader* h);
};

// ── GC engine ─────────────────────────────────────────────────────────────────

class GC {
public:
    static GC& instance();

    // called by dec_ref when rc drops to 0 or object looks cyclic
    void possible_root(GcHeader* h);

    // run the cycle detector (Bacon-Rajan mark-and-scan)
    void collect();

    // force full collection
    void collect_all();

    // stats
    size_t objects_alive()   const { return m_alive; }
    size_t bytes_alive()     const { return m_bytes_alive; }
    size_t cycles_collected() const { return m_cycles_collected; }

    // register a new object
    void register_object(GcHeader* h, size_t size);

    // threshold: run collect() when candidate buffer hits this size
    size_t collect_threshold = 1000;

private:
    GC() = default;

    std::mutex                    m_mutex;
    std::vector<GcHeader*>        m_candidates;   // purple roots
    std::atomic<size_t>           m_alive         { 0 };
    std::atomic<size_t>           m_bytes_alive   { 0 };
    std::atomic<size_t>           m_cycles_collected { 0 };

    // Bacon-Rajan phases
    void mark_roots();
    void scan_roots();
    void collect_roots();

    void mark_gray(GcHeader* h);
    void scan(GcHeader* h);
    void scan_black(GcHeader* h);
    void collect_white(GcHeader* h, std::vector<GcHeader*>& garbage);

    void free_object(GcHeader* h);
};

// ── inc/dec ref implementations ───────────────────────────────────────────────

template<typename T>
void Rc<T>::inc_ref(GcHeader* h) {
    h->rc.fetch_add(1, std::memory_order_relaxed);
    h->color = color::BLACK;
}

template<typename T>
void Rc<T>::dec_ref(GcHeader* h) {
    int32_t new_rc = h->rc.fetch_sub(1, std::memory_order_acq_rel) - 1;

    if (new_rc == 0) {
        // rc hit zero — destroy immediately
        if (h->trace) {
            h->trace(h, [](GcHeader* child) {
                // dec_ref each child
                child->rc.fetch_sub(1, std::memory_order_acq_rel);
            });
        }
        GC::instance().free_object(h);
        return;
    }

    // rc > 0 but might be a cycle root — tell the cycle detector
    if (new_rc > 0 && h->color != color::GREEN) {
        h->color = color::PURPLE;
        if (!h->buffered) {
            h->buffered = 1;
            GC::instance().possible_root(h);
        }
    }
}

// ── allocator ─────────────────────────────────────────────────────────────────

template<typename T, typename... Args>
Rc<T> gc_make(uint16_t type_id,
               void(*trace)(GcHeader*, std::function<void(GcHeader*)>),
               void(*destroy)(GcHeader*),
               Args&&... args) {
    // allocate GcHeader + T together
    uint8_t* mem = new uint8_t[sizeof(GcHeader) + sizeof(T)];
    auto* header = new(mem) GcHeader(type_id, trace, destroy);
    auto* obj    = new(mem + sizeof(GcHeader)) T(std::forward<Args>(args)...);
    GC::instance().register_object(header, sizeof(GcHeader) + sizeof(T));
    return Rc<T>(obj);
}

} // namespace jade
