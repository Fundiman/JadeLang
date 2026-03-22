#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
  #include <windows.h>
  #include <direct.h>
  #define mkdir(p,m) _mkdir(p)
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
  #include <dirent.h>
#endif

extern "C" {

// ── jade.stdlib.fs ────────────────────────────────────────────────────────────

// read entire file — returns malloc'd string, nullptr on error
const char* jade_fs_read(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

// write string to file — returns 1 on success
int jade_fs_write(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return written == len ? 1 : 0;
}

// append string to file
int jade_fs_append(const char* path, const char* content) {
    FILE* f = fopen(path, "ab");
    if (!f) return 0;
    size_t len = strlen(content);
    fwrite(content, 1, len, f);
    fclose(f);
    return 1;
}

// check if path exists
int jade_fs_exists(const char* path) {
#if defined(_WIN32)
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
#else
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
#endif
}

// check if path is a directory
int jade_fs_is_dir(const char* path) {
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES &&
            (a & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
#endif
}

// delete file
int jade_fs_delete(const char* path) {
    return remove(path) == 0 ? 1 : 0;
}

// create directory
int jade_fs_mkdir(const char* path) {
#if defined(_WIN32)
    return CreateDirectoryA(path, nullptr) ? 1 : 0;
#else
    return mkdir(path, 0755) == 0 ? 1 : 0;
#endif
}

// file size in bytes, -1 on error
int64_t jade_fs_size(const char* path) {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return -1;
    LARGE_INTEGER size;
    size.HighPart = info.nFileSizeHigh;
    size.LowPart  = info.nFileSizeLow;
    return (int64_t)size.QuadPart;
#else
    struct stat st;
    return stat(path, &st) == 0 ? (int64_t)st.st_size : -1;
#endif
}

// copy file — returns 1 on success
int jade_fs_copy(const char* src, const char* dst) {
    FILE* in  = fopen(src, "rb");
    FILE* out = fopen(dst, "wb");
    if (!in || !out) { if(in) fclose(in); if(out) fclose(out); return 0; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 1;
}

} // extern "C"
