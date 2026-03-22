#include "interp.hpp"
#include <cstring>
#include <cstdlib>

namespace jade {

extern "C" {

void* jade_interp_new() {
    return new InterpolatedString();
}

void jade_interp_literal(void* buf, const char* lit) {
    static_cast<InterpolatedString*>(buf)->add_literal(lit);
}

void jade_interp_str(void* buf, const char* val) {
    static_cast<InterpolatedString*>(buf)
        ->add_value(InterpValue::from_str(val ? val : "null"));
}

void jade_interp_int(void* buf, int64_t val) {
    static_cast<InterpolatedString*>(buf)
        ->add_value(InterpValue::from_int(val));
}

void jade_interp_float(void* buf, double val) {
    static_cast<InterpolatedString*>(buf)
        ->add_value(InterpValue::from_float(val));
}

void jade_interp_bool(void* buf, int val) {
    static_cast<InterpolatedString*>(buf)
        ->add_value(InterpValue::from_bool(val != 0));
}

const char* jade_interp_build(void* buf) {
    auto* s   = static_cast<InterpolatedString*>(buf);
    auto  str = s->build();
    // return heap-allocated C string
    char* out = (char*)malloc(str.size() + 1);
    memcpy(out, str.c_str(), str.size() + 1);
    return out;
}

void jade_interp_free(void* buf) {
    delete static_cast<InterpolatedString*>(buf);
}

} // extern "C"

} // namespace jade
