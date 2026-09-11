/**
 * @file test_timeout.cpp
 * @brief Verify timeout conversions and long-range checks through the library entry module.
 */
import std;
import mcr;

static_assert(!std::is_default_constructible_v<mcr::Timeout>);
static_assert(std::is_convertible_v<std::int32_t, mcr::Timeout>);
static_assert(std::is_convertible_v<std::chrono::seconds, mcr::Timeout>);
static_assert(std::is_same_v<decltype(std::declval<mcr::Timeout const&>().Milliseconds()), long>);

namespace {

    auto check(bool condition, std::string_view message) -> bool {
        if (!condition) {
            std::println("test_timeout: {}", message);
        }
        return condition;
    }

    auto check_construction() -> bool {
        using namespace std::chrono_literals;

        bool passed{ true };
        for (std::int32_t const count : { 0, 1, -1, 1500, (std::numeric_limits<std::int32_t>::min)(), (std::numeric_limits<std::int32_t>::max)() }) {
            mcr::Timeout const timeout{ count };
            passed &= check(
                timeout.ms.count() == count && timeout.Milliseconds() == count,
                "integer construction must preserve the full int32 millisecond range"
            );
        }

        passed &= check(mcr::Timeout{ 1250ms }.Milliseconds() == 1250, "milliseconds must preserve their count");
        passed &= check(mcr::Timeout{ 2s }.Milliseconds() == 2000, "seconds must convert to milliseconds");
        passed &= check(mcr::Timeout{ 3min }.Milliseconds() == 180000, "minutes must convert to milliseconds");
        passed &= check(mcr::Timeout{ 1h }.Milliseconds() == 3600000, "hours must convert to milliseconds");
        passed &= check(
            mcr::Timeout{ 1999us }.Milliseconds() == 1 && mcr::Timeout{ -1999us }.Milliseconds() == -1,
            "sub-millisecond remainders must truncate toward zero"
        );
        passed &= check(
            mcr::Timeout{ 999999ns }.Milliseconds() == 0 && mcr::Timeout{ -999999ns }.Milliseconds() == 0,
            "durations shorter than one millisecond must truncate to zero"
        );
        passed &= check(
            mcr::Timeout{ std::chrono::duration<double>{ 1.2345 } }.Milliseconds() == 1234 &&
                mcr::Timeout{ std::chrono::duration<double>{ -1.2345 } }.Milliseconds() == -1234,
            "finite floating-point durations must truncate fractional milliseconds toward zero"
        );
        passed &= check(
            mcr::Timeout{ std::chrono::duration<int, std::ratio<1, 3>>{ 2 } }.Milliseconds() == 666,
            "custom duration periods must convert to whole milliseconds"
        );

        mcr::Timeout       timeout = 1500;
        mcr::Timeout const copied{ timeout };
        timeout.ms  = 2s;
        passed     &= check(
            copied.Milliseconds() == 1500 && timeout.Milliseconds() == 2000,
            "copies must be independent and Milliseconds must observe public ms updates"
        );
        timeout  = 3s;
        passed  &= check(timeout.Milliseconds() == 3000, "chrono durations must implicitly convert for assignment");
        return passed;
    }

    auto check_range() -> bool {
        using MillisecondsRep = std::chrono::milliseconds::rep;

        constexpr auto LONG_MIN_MS{ static_cast<MillisecondsRep>((std::numeric_limits<long>::min)()) };
        constexpr auto LONG_MAX_MS{ static_cast<MillisecondsRep>((std::numeric_limits<long>::max)()) };

        bool passed{ true };
        passed &= check(
            mcr::Timeout{ std::chrono::milliseconds{ LONG_MIN_MS } }.Milliseconds() == (std::numeric_limits<long>::min)() &&
                mcr::Timeout{ std::chrono::milliseconds{ LONG_MAX_MS } }.Milliseconds() == (std::numeric_limits<long>::max)(),
            "both long boundaries must convert without throwing or losing precision"
        );

        if constexpr (std::numeric_limits<MillisecondsRep>::digits > std::numeric_limits<long>::digits) {
            mcr::Timeout timeout{ std::chrono::milliseconds{ LONG_MAX_MS } };
            ++timeout.ms;
            try {
                (void)timeout.Milliseconds();
                passed &= check(false, "a count above long max must throw overflow_error");
            } catch (std::overflow_error const& error) {
                passed &= check(
                    std::string_view{ error.what() } == std::format("mcr::Timeout: timeout value overflow: {} ms.", timeout.ms.count()),
                    "overflow diagnostics must include the original millisecond count"
                );
            }

            timeout.ms = std::chrono::milliseconds{ LONG_MIN_MS };
            --timeout.ms;
            try {
                (void)timeout.Milliseconds();
                passed &= check(false, "a count below long min must throw underflow_error");
            } catch (std::underflow_error const& error) {
                passed &= check(
                    std::string_view{ error.what() } == std::format("mcr::Timeout: timeout value underflow: {} ms.", timeout.ms.count()),
                    "underflow diagnostics must include the original millisecond count"
                );
            }

            timeout.ms  = std::chrono::milliseconds{ 42 };
            passed     &= check(timeout.Milliseconds() == 42, "a timeout must remain usable after a failed conversion");
        }
        return passed;
    }

} // namespace

int main() {
    bool passed{ check_construction() };
    passed &= check_range();
    if (!passed) {
        return 1;
    }
    std::println("test_timeout: ok");
    return 0;
}
