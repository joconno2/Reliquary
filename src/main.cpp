#include "core/engine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#endif

#ifndef RELIQUARY_VERSION
#define RELIQUARY_VERSION "dev"
#endif

// Exe directory (for assets/data), resolved once at startup
static char exe_dir[4096] = ".";

// Log directory (writable location for crash/log files)
// On Windows: %LOCALAPPDATA%\Reliquary\  (always writable)
// On Linux: same as exe_dir
static char log_dir[4096] = ".";

// Build an absolute path into a directory for the given filename
static void make_path(char* out, size_t out_sz, const char* dir, const char* filename) {
    snprintf(out, out_sz, "%s%c%s", dir,
#ifdef _WIN32
             '\\',
#else
             '/',
#endif
             filename);
}

// Set working directory to the exe's directory (Steam may launch from elsewhere)
static void fix_working_directory([[maybe_unused]] const char* argv0) {
#ifdef _WIN32
    GetModuleFileNameA(nullptr, exe_dir, sizeof(exe_dir));
    char* last_slash = strrchr(exe_dir, '\\');
    if (last_slash) *last_slash = '\0';
    SetCurrentDirectoryA(exe_dir);

    // Log directory: %LOCALAPPDATA%\Reliquary
    char appdata[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata) == S_OK) {
        snprintf(log_dir, sizeof(log_dir), "%s\\Reliquary", appdata);
        CreateDirectoryA(log_dir, nullptr);
    } else {
        // Fallback to exe directory
        strncpy(log_dir, exe_dir, sizeof(log_dir) - 1);
    }
#else
    // On Linux/macOS, only chdir if argv0 is an absolute path (e.g. Steam launcher).
    // Relative paths like ./build/reliquary mean the user is already in the repo root.
    if (argv0 && argv0[0] == '/') {
        strncpy(exe_dir, argv0, sizeof(exe_dir) - 1);
        exe_dir[sizeof(exe_dir) - 1] = '\0';
        char* last_slash = strrchr(exe_dir, '/');
        if (last_slash && last_slash != exe_dir) {
            *last_slash = '\0';
            chdir(exe_dir);
        }
    }
    strncpy(log_dir, exe_dir, sizeof(log_dir) - 1);
#endif
}

#ifdef _WIN32
// Gather system info into a buffer. Used by both crash log and MessageBox.
static int append_system_info(char* buf, size_t sz) {
    int n = 0;
    OSVERSIONINFOA vi = {};
    vi.dwOSVersionInfoSize = sizeof(vi);
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (GetVersionExA(&vi)) {
        n += snprintf(buf + n, sz - n, "Windows: %lu.%lu build %lu\n",
                      vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    }
    #pragma GCC diagnostic pop
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        n += snprintf(buf + n, sz - n, "RAM: %llu MB free / %llu MB total\n",
                      (unsigned long long)(mem.ullAvailPhys / (1024*1024)),
                      (unsigned long long)(mem.ullTotalPhys / (1024*1024)));
    }
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw > 0 && sh > 0) {
        n += snprintf(buf + n, sz - n, "Display: %dx%d\n", sw, sh);
    }
    return n;
}

// Resolve module name from an address (e.g. "reliquary.exe" or "SDL2.dll")
static void get_module_at_addr(void* addr, char* out, size_t out_sz) {
    out[0] = '\0';
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)addr, &mod) && mod) {
        char fullpath[MAX_PATH];
        if (GetModuleFileNameA(mod, fullpath, MAX_PATH)) {
            const char* name = strrchr(fullpath, '\\');
            name = name ? name + 1 : fullpath;
            strncpy(out, name, out_sz - 1);
            out[out_sz - 1] = '\0';
        }
    }
}
#endif

// Write a crash log with as much context as possible
static void write_crash_log(const char* reason) {
    char path[4096];
    make_path(path, sizeof(path), log_dir, "reliquary_crash.txt");
    FILE* f = fopen(path, "w");
    if (!f) {
        // Fallback: try exe directory
        make_path(path, sizeof(path), exe_dir, "reliquary_crash.txt");
        f = fopen(path, "w");
    }
    if (!f) return;
    fprintf(f, "Reliquary %s crashed.\n\n", RELIQUARY_VERSION);
    fprintf(f, "Reason: %s\n", reason);
    fprintf(f, "Exe directory: %s\n", exe_dir);
    fprintf(f, "Log directory: %s\n", log_dir);
#ifdef _WIN32
    char sysinfo[512];
    append_system_info(sysinfo, sizeof(sysinfo));
    fprintf(f, "%s", sysinfo);
#endif
    fprintf(f, "\nAlso check reliquary_log.txt in the same folder for init details.\n");
    fclose(f);
}

#ifdef _WIN32
// Windows: catch crashes via SEH and write a useful crash file
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    DWORD code = 0;
    void* addr = nullptr;
    const char* desc = "Unknown exception";

    if (ep && ep->ExceptionRecord) {
        code = ep->ExceptionRecord->ExceptionCode;
        addr = ep->ExceptionRecord->ExceptionAddress;
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:    desc = "Access violation (bad memory read/write)"; break;
            case EXCEPTION_STACK_OVERFLOW:      desc = "Stack overflow"; break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:  desc = "Integer divide by zero"; break;
            case EXCEPTION_ILLEGAL_INSTRUCTION: desc = "Illegal CPU instruction"; break;
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:  desc = "Float divide by zero"; break;
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: desc = "Array bounds exceeded"; break;
            case EXCEPTION_IN_PAGE_ERROR:       desc = "Page fault (disk or memory error)"; break;
            default: break;
        }
    }

    // Access violation detail: read vs write, and what address was targeted
    char av_detail[128] = "";
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        const char* op = ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing";
        snprintf(av_detail, sizeof(av_detail), "Attempted %s address 0x%llx\n",
                 op, (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    // Module where crash occurred
    char module[128] = "unknown";
    if (addr) get_module_at_addr(addr, module, sizeof(module));

    // System info
    char sysinfo[512] = "";
    append_system_info(sysinfo, sizeof(sysinfo));

    // Write crash file (best effort, may fail if disk is full etc.)
    write_crash_log(desc);
    {
        char path[4096];
        make_path(path, sizeof(path), log_dir, "reliquary_crash.txt");
        FILE* f = fopen(path, "a");
        if (f) {
            fprintf(f, "\nException code: 0x%08lX\n", code);
            fprintf(f, "Crash address: 0x%p  [%s]\n", addr, module);
            if (av_detail[0]) fprintf(f, "%s", av_detail);
            fclose(f);
        }
    }

    // Append to main log too
    fflush(stderr);
    fprintf(stderr, "\n=== CRASH: %s (0x%08lX) at %p [%s] ===\n", desc, code, addr, module);
    fflush(stderr);

    // MessageBox: everything a tester needs in one screenshot.
    // This works even if all file I/O above failed.
    char msg[2048];
    int n = 0;
    n += snprintf(msg + n, sizeof(msg) - n,
                  "Reliquary %s crashed.\n\n", RELIQUARY_VERSION);
    n += snprintf(msg + n, sizeof(msg) - n,
                  "WHAT HAPPENED:\n"
                  "  %s\n", desc);
    n += snprintf(msg + n, sizeof(msg) - n,
                  "  Exception 0x%08lX at address %p\n"
                  "  Module: %s\n", code, addr, module);
    if (av_detail[0]) {
        n += snprintf(msg + n, sizeof(msg) - n, "  %s", av_detail);
    }
    n += snprintf(msg + n, sizeof(msg) - n,
                  "\nSYSTEM:\n"
                  "  %s", sysinfo);
    n += snprintf(msg + n, sizeof(msg) - n,
                  "\nLOG FILES (please include when reporting):\n"
                  "  %s\\reliquary_crash.txt\n"
                  "  %s\\reliquary_log.txt\n",
                  log_dir, log_dir);
    n += snprintf(msg + n, sizeof(msg) - n,
                  "\nYou can copy this text with Ctrl+C.");

    MessageBoxA(nullptr, msg, "Reliquary Crash", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
// Unix: catch fatal signals
static void crash_signal_handler(int sig) {
    const char* desc = "Unknown signal";
    switch (sig) {
        case SIGSEGV: desc = "Segmentation fault (SIGSEGV)"; break;
        case SIGABRT: desc = "Abort (SIGABRT)"; break;
        case SIGFPE:  desc = "Floating point exception (SIGFPE)"; break;
        case SIGBUS:  desc = "Bus error (SIGBUS)"; break;
        default: break;
    }
    write_crash_log(desc);
    fprintf(stderr, "\n=== CRASH: %s ===\n", desc);
    fflush(stderr);
    // Re-raise with default handler so the OS can produce a core dump
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

// Redirect stdout/stderr to a log file on Windows (no console window)
static void setup_logging() {
    char path[4096];
    make_path(path, sizeof(path), log_dir, "reliquary_log.txt");
#ifdef _WIN32
    freopen(path, "a", stderr);
    freopen(path, "a", stdout);
#endif
    fprintf(stderr, "Reliquary %s starting...\n", RELIQUARY_VERSION);
    fprintf(stderr, "Exe directory: %s\n", exe_dir);
    fprintf(stderr, "Log directory: %s\n", log_dir);
    fflush(stderr);
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    fix_working_directory(argv[0]);

    // Install crash handlers ASAP
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_handler);
#else
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGFPE,  crash_signal_handler);
    signal(SIGBUS,  crash_signal_handler);
#endif

    // Write startup marker before any SDL init.
    // setup_logging appends to this file, so the marker survives.
    {
        char path[4096];
        make_path(path, sizeof(path), log_dir, "reliquary_log.txt");
        FILE* f = fopen(path, "w");
        if (f) {
            fprintf(f, "Reliquary %s\n", RELIQUARY_VERSION);
            fprintf(f, "Exe: %s\n", exe_dir);
            fprintf(f, "Logs: %s\n", log_dir);
            fprintf(f, "---\n");
            fclose(f);
        }
    }

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
        // Read back the log to find the last [init] line (the step that failed)
        char last_init[256] = "unknown step";
        {
            char logpath[4096];
            make_path(logpath, sizeof(logpath), log_dir, "reliquary_log.txt");
            FILE* lf = fopen(logpath, "r");
            if (lf) {
                char line[256];
                while (fgets(line, sizeof(line), lf)) {
                    if (strstr(line, "[init]")) {
                        size_t len = strlen(line);
                        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
                        strncpy(last_init, line, sizeof(last_init) - 1);
                    }
                }
                fclose(lf);
            }
        }
        char sysinfo[512] = "";
        append_system_info(sysinfo, sizeof(sysinfo));
        char msg[2048];
        snprintf(msg, sizeof(msg),
                 "Reliquary %s failed to start.\n\n"
                 "FAILED AT:\n"
                 "  %s\n\n"
                 "SYSTEM:\n"
                 "  %s\n"
                 "COMMON FIXES:\n"
                 "  - Update your graphics drivers\n"
                 "  - Make sure 'assets' and 'data' folders are next to reliquary.exe\n"
                 "  - Try running as administrator\n\n"
                 "LOG FILE (please include when reporting):\n"
                 "  %s\\reliquary_log.txt\n\n"
                 "You can copy this text with Ctrl+C.",
                 RELIQUARY_VERSION, last_init, sysinfo, log_dir);
        MessageBoxA(nullptr, msg, "Reliquary Error", MB_OK | MB_ICONERROR);
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
