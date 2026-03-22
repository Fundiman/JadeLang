#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>

#if defined(__linux__)
  #include <unistd.h>
  #include <sys/utsname.h>
  static const char* PLATFORM = "linux";
  static const char* ARCH =
  #if defined(__x86_64__)
    "x86_64";
  #elif defined(__aarch64__)
    "arm64";
  #else
    "unknown";
  #endif
#elif defined(__APPLE__)
  #include <unistd.h>
  static const char* PLATFORM = "macos";
  static const char* ARCH =
  #if defined(__x86_64__)
    "x86_64";
  #elif defined(__aarch64__)
    "arm64";
  #else
    "unknown";
  #endif
#elif defined(_WIN32)
  #include <windows.h>
  static const char* PLATFORM = "windows";
  static const char* ARCH = "x86_64";
#else
  static const char* PLATFORM = "unknown";
  static const char* ARCH = "unknown";
#endif

extern "C" {

// ── jade.stdlib.os ────────────────────────────────────────────────────────────

const char* jade_os_platform() { return PLATFORM; }
const char* jade_os_arch()     { return ARCH;     }

const char* jade_os_env(const char* key) {
    const char* v = getenv(key);
    return v ? v : "";
}

void jade_os_exit(int32_t code) {
    exit(code);
}

int32_t jade_os_getpid() {
#if defined(_WIN32)
    return (int32_t)GetCurrentProcessId();
#else
    return (int32_t)getpid();
#endif
}

// current working directory — returns malloc'd string
const char* jade_os_cwd() {
#if defined(_WIN32)
    char buf[4096];
    GetCurrentDirectoryA(sizeof(buf), buf);
    char* out = (char*)malloc(strlen(buf) + 1);
    strcpy(out, buf);
    return out;
#else
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return strdup(".");
    return strdup(buf);
#endif
}

// set environment variable
void jade_os_setenv(const char* key, const char* val) {
#if defined(_WIN32)
    SetEnvironmentVariableA(key, val);
#else
    setenv(key, val, 1);
#endif
}

} // extern "C"
