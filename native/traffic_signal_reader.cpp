#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>

// Reads only the documented ETS2LA output mappings. No process-memory access,
// pattern scans, input mappings, injection, or game-file writes.
// Layout: ETS2LA/plugin commit 7094b334f10b68343d0082f1ef4078669235106b.
static bool copy_mapping(const wchar_t* name, unsigned char* output, size_t bytes) {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!mapping) return false;
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, bytes);
    if (!view) { CloseHandle(mapping); return false; }
    std::memcpy(output, view, bytes);
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return true;
}

int main(int argc, char** argv) {
    const bool once = argc == 2 && std::strcmp(argv[1], "--once") == 0;
    if (argc > 1 && !once) return 2;
    unsigned sequence = 0;
    do {
        std::array<unsigned char, 6> status{}, status_again{};
        std::array<unsigned char, 1920> sample{}, sample_again{};
        // Close all handles after every copy: our reader cannot keep a dead
        // producer's mapping alive. Repeated reads reject visibly torn copies.
        const bool available =
            copy_mapping(L"Local\\ETS2LAPluginStatus", status.data(), status.size()) &&
            copy_mapping(L"Local\\ETS2LASemaphore", sample.data(), sample.size()) &&
            copy_mapping(L"Local\\ETS2LASemaphore", sample_again.data(), sample_again.size()) &&
            copy_mapping(L"Local\\ETS2LAPluginStatus", status_again.data(), status_again.size()) &&
            status == status_again && sample == sample_again;
        if (available) {
            std::int32_t version = 0;
            std::memcpy(&version, status.data(), sizeof(version));
            std::array<char, 3841> hex{};
            const char alphabet[] = "0123456789abcdef";
            for (size_t i = 0; i < sample.size(); ++i) {
                hex[i * 2] = alphabet[sample[i] >> 4];
                hex[i * 2 + 1] = alphabet[sample[i] & 15];
            }
            std::printf("{\"protocol\":1,\"source\":\"ets2la-semaphore-reader\",\"sequence\":%u,\"connected\":true,\"pluginVersion\":%d,\"data\":\"%s\"}\n",
                ++sequence, version, hex.data());
        } else {
            std::printf("{\"protocol\":1,\"source\":\"ets2la-semaphore-reader\",\"sequence\":%u,\"connected\":false}\n", ++sequence);
        }
        if (std::fflush(stdout) != 0 || std::ferror(stdout)) return 0;
        if (!once) Sleep(200);
    } while (!once);
    return 0;
}
