# Request timeout

Import `mcr` or `mcr.timeout` to use `mcr::Timeout`.

```cpp
import std;
import mcr;

using namespace std::chrono_literals;

mcr::Timeout const from_integer{ 1500 };
mcr::Timeout timeout{ 2s };
std::println("{} ms", timeout.Milliseconds()); // 2000 ms
timeout.ms = 500ms;
```

`Timeout` accepts an integer millisecond count (`std::int32_t`) or any
`std::chrono::duration<Rep, Period>`. Both constructors allow implicit
conversion. There is no default constructor; use `Timeout{0}` to represent
no overall request timeout when passed to curl. The public `ms` member stores
`std::chrono::milliseconds` and can be updated after construction.

Chrono construction uses `std::chrono::duration_cast`, truncating fractional
milliseconds toward zero. For example, `1999us` becomes `1ms` and `-1999us`
becomes `-1ms`. As in cpr, callers must supply durations that convert within
the milliseconds representation without overflowing the conversion arithmetic;
floating-point durations must also be finite.

`Milliseconds()` returns the `long` required by curl's `CURLOPT_TIMEOUT_MS`.
It throws `std::overflow_error` above `LONG_MAX` and `std::underflow_error`
below `LONG_MIN`, including the stored count in the diagnostic. Range checks
occur when reading the value, so they also apply to changes made through `ms`.
Zero and representable negative values are preserved without validation.

The interface and behavior follow cpr's `include/cpr/timeout.h` and
`cpr/timeout.cpp`. Intentional differences are the C++23 module exports,
namespace `mcr`, diagnostic prefix `mcr::Timeout`, and passing the integer
constructor argument by value instead of by const reference.

Run `mcpp build` and `mcpp test` to validate conversions, truncation, public
member updates, and platform-dependent range checks. This module represents
the timeout option; request/session integration will apply it to curl.
