#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <algorithm>

// ── jade.stdlib.collections ───────────────────────────────────────────────────
// List<T>, Map<K,V>, Set<T> backed by C++ stdlib
// values stored as void* — Jade's RC system manages lifetime

extern "C" {

// ── List ──────────────────────────────────────────────────────────────────────

void*   jade_list_new()                        { return new std::vector<void*>(); }
void    jade_list_free(void* l)                { delete (std::vector<void*>*)l; }
void    jade_list_push(void* l, void* v)       { ((std::vector<void*>*)l)->push_back(v); }
void*   jade_list_get(void* l, int32_t i)      {
    auto* v = (std::vector<void*>*)l;
    if (i < 0 || i >= (int32_t)v->size()) return nullptr;
    return (*v)[i];
}
void    jade_list_set(void* l, int32_t i, void* v) {
    auto* vec = (std::vector<void*>*)l;
    if (i >= 0 && i < (int32_t)vec->size()) (*vec)[i] = v;
}
int32_t jade_list_len(void* l)                 { return (int32_t)((std::vector<void*>*)l)->size(); }
int     jade_list_empty(void* l)               { return ((std::vector<void*>*)l)->empty() ? 1 : 0; }
void    jade_list_clear(void* l)               { ((std::vector<void*>*)l)->clear(); }
void*   jade_list_pop(void* l) {
    auto* v = (std::vector<void*>*)l;
    if (v->empty()) return nullptr;
    void* last = v->back();
    v->pop_back();
    return last;
}
void*   jade_list_first(void* l) {
    auto* v = (std::vector<void*>*)l;
    return v->empty() ? nullptr : v->front();
}
void*   jade_list_last(void* l) {
    auto* v = (std::vector<void*>*)l;
    return v->empty() ? nullptr : v->back();
}
void    jade_list_remove_at(void* l, int32_t i) {
    auto* v = (std::vector<void*>*)l;
    if (i >= 0 && i < (int32_t)v->size())
        v->erase(v->begin() + i);
}
void    jade_list_insert(void* l, int32_t i, void* val) {
    auto* v = (std::vector<void*>*)l;
    if (i >= 0 && i <= (int32_t)v->size())
        v->insert(v->begin() + i, val);
}

// ── Map<str, void*> ───────────────────────────────────────────────────────────

void*  jade_map_new()  { return new std::unordered_map<std::string, void*>(); }
void   jade_map_free(void* m) { delete (std::unordered_map<std::string,void*>*)m; }

void   jade_map_set(void* m, const char* k, void* v) {
    (*(std::unordered_map<std::string,void*>*)m)[k] = v;
}
void*  jade_map_get(void* m, const char* k) {
    auto& map = *(std::unordered_map<std::string,void*>*)m;
    auto it = map.find(k);
    return it != map.end() ? it->second : nullptr;
}
int    jade_map_has(void* m, const char* k) {
    return ((std::unordered_map<std::string,void*>*)m)->count(k) > 0 ? 1 : 0;
}
void   jade_map_delete(void* m, const char* k) {
    ((std::unordered_map<std::string,void*>*)m)->erase(k);
}
int32_t jade_map_len(void* m) {
    return (int32_t)((std::unordered_map<std::string,void*>*)m)->size();
}
void   jade_map_clear(void* m) {
    ((std::unordered_map<std::string,void*>*)m)->clear();
}

// ── Set<str> ──────────────────────────────────────────────────────────────────

void*  jade_set_new()  { return new std::unordered_set<std::string>(); }
void   jade_set_free(void* s) { delete (std::unordered_set<std::string>*)s; }

void   jade_set_add(void* s, const char* v) {
    ((std::unordered_set<std::string>*)s)->insert(v);
}
int    jade_set_has(void* s, const char* v) {
    return ((std::unordered_set<std::string>*)s)->count(v) > 0 ? 1 : 0;
}
void   jade_set_remove(void* s, const char* v) {
    ((std::unordered_set<std::string>*)s)->erase(v);
}
int32_t jade_set_len(void* s) {
    return (int32_t)((std::unordered_set<std::string>*)s)->size();
}
void   jade_set_clear(void* s) {
    ((std::unordered_set<std::string>*)s)->clear();
}
int    jade_set_empty(void* s) {
    return ((std::unordered_set<std::string>*)s)->empty() ? 1 : 0;
}

} // extern "C"
