#include "core/engine.h"
#include <cstdio>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    Engine engine;

    if (!engine.init()) {
        fprintf(stderr, "Engine initialization failed\n");
        return 1;
    }

    engine.run();
    return 0;
}
