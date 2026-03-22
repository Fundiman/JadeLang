#include "interp.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// ── jade.stdlib.io ────────────────────────────────────────────────────────────
// all functions exported with C linkage so jvk/RuntimeDyld can resolve them

extern "C" {

// println(str) — print with newline
void jade_io_println(const char* s) {
    printf("%s\n", s ? s : "null");
}

// print(str) — print without newline
void jade_io_print(const char* s) {
    printf("%s", s ? s : "null");
}

// println_int(i32)
void jade_io_println_int(int32_t v) {
    printf("%d\n", v);
}

// println_float(f64)
void jade_io_println_float(double v) {
    printf("%g\n", v);
}

// println_bool(bool)
void jade_io_println_bool(int v) {
    printf("%s\n", v ? "true" : "false");
}

// readln() — read a line from stdin, returns malloc'd string
const char* jade_io_readln() {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return nullptr;
    // strip trailing newline
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    char* out = (char*)malloc(len + 1);
    memcpy(out, buf, len + 1);
    return out;
}

// readInt() — read an integer from stdin
int32_t jade_io_readInt() {
    int32_t v = 0;
    scanf("%d", &v);
    return v;
}

// stderr_println(str)
void jade_io_stderr(const char* s) {
    fprintf(stderr, "%s\n", s ? s : "null");
}

// flush stdout
void jade_io_flush() {
    fflush(stdout);
}

// ── interpolated println ──────────────────────────────────────────────────────
// jade_io_println_interp(template, n_vars, names[], values[])
// this is what the compiler actually emits for:
//   io.println("hello #{name}, score=#{score}")

void jade_io_println_interp(const char* tmpl,
                             int n_vars,
                             const char** names,
                             const char** str_values) {
    // build the interpolated string
    std::string result;
    result.reserve(256);

    const char* p = tmpl;
    while (*p) {
        if (p[0] == '#' && p[1] == '{') {
            const char* start = p + 2;
            const char* end   = strchr(start, '}');
            if (!end) { result += p; break; }

            std::string var_name(start, end - start);

            // find in names array
            bool found = false;
            for (int i = 0; i < n_vars; i++) {
                if (var_name == names[i]) {
                    result += str_values[i];
                    found = true;
                    break;
                }
            }
            if (!found) {
                result += "#{" + var_name + "}";
            }
            p = end + 1;
        } else {
            result += *p++;
        }
    }

    printf("%s\n", result.c_str());
}

} // extern "C"
