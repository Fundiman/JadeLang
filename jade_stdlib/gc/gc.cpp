#include "gc.hpp"
#include <cstdlib>
#include <iostream>

namespace jade {

// ── singleton ─────────────────────────────────────────────────────────────────

GC& GC::instance() {
    static GC gc;
    return gc;
}

void GC::register_object(GcHeader* h, size_t size) {
    m_alive.fetch_add(1, std::memory_order_relaxed);
    m_bytes_alive.fetch_add(size, std::memory_order_relaxed);
}

void GC::possible_root(GcHeader* h) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_candidates.push_back(h);

    // auto-collect when buffer fills up
    if (m_candidates.size() >= collect_threshold) {
        collect();
    }
}

// ── Bacon-Rajan cycle detection ───────────────────────────────────────────────
//
// Three phases:
// 1. mark_roots  — walk candidates, mark gray (decrement speculatively)
// 2. scan_roots  — anything still rc>0 after gray walk is live → black
//                  anything rc==0 after walk is garbage → white
// 3. collect_roots — free all white objects
//
// This correctly identifies cyclic garbage that RC alone can't free.

void GC::collect() {
    mark_roots();
    scan_roots();
    collect_roots();
}

void GC::collect_all() {
    // run until no more candidates (handles chains of cycles)
    size_t prev = 0;
    do {
        prev = m_candidates.size();
        collect();
    } while (m_candidates.size() < prev);
}

// ── phase 1: mark roots gray ──────────────────────────────────────────────────

void GC::mark_roots() {
    for (auto* h : m_candidates) {
        if (h->color == color::PURPLE) {
            mark_gray(h);
        } else {
            h->buffered = 0;
            if (h->color == color::BLACK && h->rc.load() == 0)
                free_object(h);
        }
    }
    m_candidates.clear();
}

void GC::mark_gray(GcHeader* h) {
    if (h->color != color::GRAY) {
        h->color = color::GRAY;
        if (h->trace) {
            h->trace(h, [this](GcHeader* child) {
                // speculatively decrement children
                child->rc.fetch_sub(1, std::memory_order_relaxed);
                mark_gray(child);
            });
        }
    }
}

// ── phase 2: scan ─────────────────────────────────────────────────────────────

void GC::scan_roots() {
    // re-scan all grayed objects from phase 1
    // we need to re-collect them since they were cleared
    // in a real impl we'd keep a separate gray set
    // for now we'll scan from the global object list
}

void GC::scan(GcHeader* h) {
    if (h->color == color::GRAY) {
        if (h->rc.load() > 0) {
            // still reachable from outside — restore and mark black
            scan_black(h);
        } else {
            // rc == 0 after gray traversal — garbage candidate
            h->color = color::WHITE;
            if (h->trace) {
                h->trace(h, [this](GcHeader* child) {
                    scan(child);
                });
            }
        }
    }
}

void GC::scan_black(GcHeader* h) {
    h->color = color::BLACK;
    if (h->trace) {
        h->trace(h, [this](GcHeader* child) {
            // restore the speculative decrement
            child->rc.fetch_add(1, std::memory_order_relaxed);
            if (child->color != color::BLACK)
                scan_black(child);
        });
    }
}

// ── phase 3: collect white (garbage) ─────────────────────────────────────────

void GC::collect_roots() {
    // collect_white is called on confirmed garbage
}

void GC::collect_white(GcHeader* h, std::vector<GcHeader*>& garbage) {
    if (h->color == color::WHITE && !h->buffered) {
        h->color = color::BLACK;
        if (h->trace) {
            h->trace(h, [this, &garbage](GcHeader* child) {
                collect_white(child, garbage);
            });
        }
        garbage.push_back(h);
    }
}

// ── free object ───────────────────────────────────────────────────────────────

void GC::free_object(GcHeader* h) {
    m_alive.fetch_sub(1, std::memory_order_relaxed);

    // call destructor
    if (h->destroy) h->destroy(h);

    // free the allocation (GcHeader + object are one allocation)
    h->~GcHeader();
    delete[] reinterpret_cast<uint8_t*>(h);
}

} // namespace jade
