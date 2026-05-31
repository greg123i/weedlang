#include <cassert>
#include <cstdint>
#include <iostream>

extern "C" std::int64_t wl_weed_add(std::int64_t a, std::int64_t b);

int main() {
    const std::int64_t result = wl_weed_add(19, 23);
    assert(result == 42);
    std::cout << "WeedLang .obj interop test passed!\n";
    return 0;
}
