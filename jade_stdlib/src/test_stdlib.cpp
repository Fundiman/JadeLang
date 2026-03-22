#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

// declare the stdlib symbols we're testing
extern "C" {
    void        jade_io_println(const char* s);
    void        jade_io_println_int(int32_t v);
    void        jade_io_println_float(double v);
    void        jade_io_println_bool(int v);
    void        jade_io_println_interp(const char* tmpl, int n,
                                        const char** names,
                                        const char** vals);
    double      jade_math_sqrt(double x);
    double      jade_math_PI();
    int32_t     jade_math_rand_int(int32_t lo, int32_t hi);
    const char* jade_os_platform();
    const char* jade_os_arch();
    int         jade_fs_exists(const char* path);
    void*       jade_list_new();
    void        jade_list_push(void* l, void* v);
    int32_t     jade_list_len(void* l);
    void*       jade_list_get(void* l, int32_t i);
    void*       jade_interp_new();
    void        jade_interp_literal(void* buf, const char* lit);
    void        jade_interp_str(void* buf, const char* val);
    void        jade_interp_int(void* buf, int64_t val);
    const char* jade_interp_build(void* buf);
    void        jade_interp_free(void* buf);
}

int main() {
    jade_io_println("── jade stdlib test ──────────────────");

    // ── string interpolation ──────────────────────────────────────────────
    jade_io_println("testing: string interpolation");

    // "hello #{name}!" with name = "Jade"
    const char* names[]  = { "name", "version" };
    const char* values[] = { "Jade", "0.1.0-alpha" };
    jade_io_println_interp("hello #{name} v#{version}!", 2, names, values);

    // InterpolatedString builder API
    void* buf = jade_interp_new();
    jade_interp_literal(buf, "pi = ");
    jade_interp_int(buf, 3);
    jade_interp_literal(buf, ".");
    jade_interp_int(buf, 14159);
    const char* result = jade_interp_build(buf);
    jade_io_println(result);
    free((void*)result);
    jade_interp_free(buf);

    // ── math ──────────────────────────────────────────────────────────────
    jade_io_println("testing: math");
    jade_io_println_float(jade_math_sqrt(144.0));
    jade_io_println_float(jade_math_PI());
    jade_io_println_int(jade_math_rand_int(1, 100));

    // ── os ────────────────────────────────────────────────────────────────
    jade_io_println("testing: os");
    jade_io_println(jade_os_platform());
    jade_io_println(jade_os_arch());

    // ── fs ────────────────────────────────────────────────────────────────
    jade_io_println("testing: fs");
    jade_io_println_bool(jade_fs_exists("/tmp"));
    jade_io_println_bool(jade_fs_exists("/nonexistent_jade_path_xyz"));

    // ── collections ───────────────────────────────────────────────────────
    jade_io_println("testing: collections");
    void* list = jade_list_new();
    jade_list_push(list, (void*)"apple");
    jade_list_push(list, (void*)"banana");
    jade_list_push(list, (void*)"cherry");
    jade_io_println_int(jade_list_len(list));
    jade_io_println((const char*)jade_list_get(list, 1));

    jade_io_println("── all tests passed ──────────────────");
    return 0;
}
