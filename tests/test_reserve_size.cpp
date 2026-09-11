/**
 * @file test_reserve_size.cpp
 * @brief Verify reserve size compatibility and boundaries through the library entry module.
 */
import std;
import mcr;

static_assert(!std::is_default_constructible_v<mcr::ReserveSize>);
static_assert(std::is_convertible_v<std::size_t, mcr::ReserveSize>);
static_assert(std::is_same_v<decltype(mcr::ReserveSize::size), std::size_t>);

int main() {
    for (std::size_t const size : { std::size_t{ 0 }, std::size_t{ 1 }, std::size_t{ 4096 }, (std::numeric_limits<std::size_t>::max)() }) {
        mcr::ReserveSize const option = size;
        if (option.size != size) {
            std::println("test_reserve_size: implicit construction must preserve {} without allocation or truncation", size);
            return 1;
        }
    }

    mcr::ReserveSize option{ 4096 };
    mcr::ReserveSize copied{ option };
    option.size = 0;
    if (copied.size != 4096 || option.size != 0) {
        std::println("test_reserve_size: public size updates must leave copies independent");
        return 1;
    }
    copied = std::size_t{ 8192 };
    if (copied.size != 8192) {
        std::println("test_reserve_size: size_t assignment must implicitly construct an option");
        return 1;
    }

    std::println("test_reserve_size: ok");
    return 0;
}
