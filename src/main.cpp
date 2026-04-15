#include "core/engine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Set working directory to the exe's directory (Steam may launch from elsewhere)
static void fix_working_directory([[maybe_unused]] const char* argv0) {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    // Strip the exe name to get the directory
    char* last_slash = strrchr(path, '\\');
    if (last_slash) { *last_slash = '\0'; SetCurrentDirectoryA(path); }
#else
    // On Linux/macOS, try to cd to the exe's directory
    if (argv0) {
        char buf[4096];
        strncpy(buf, argv0, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* last_slash = strrchr(buf, '/');
        if (last_slash && last_slash != buf) {
            *last_slash = '\0';
            chdir(buf);
        }
    }
#endif
}

// Redirect stdout/stderr to a log file on Windows (no console)
static void setup_logging() {
#ifdef _WIN32
    freopen("reliquary_log.txt", "w", stderr);
    freopen("reliquary_log.txt", "a", stdout);
#endif
    fprintf(stderr, "Reliquary starting...\n");
    fflush(stderr);
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    fix_working_directory(argv[0]);
    setup_logging();

    fprintf(stderr, "Creating engine...\n");
    fflush(stderr);

    Engine engine;

    fprintf(stderr, "Initializing engine...\n");
    fflush(stderr);

    if (!engine.init()) {
        fprintf(stderr, "Engine initialization failed\n");
        fflush(stderr);
#ifdef _WIN32
        MessageBoxA(nullptr, "Engine initialization failed.\nCheck reliquary_log.txt for details.",
                     "Reliquary Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }

    fprintf(stderr, "Engine initialized. Starting game loop.\n");
    fflush(stderr);

    engine.run();

    fprintf(stderr, "Game exited normally.\n");
    fflush(stderr);

    return 0;
}
