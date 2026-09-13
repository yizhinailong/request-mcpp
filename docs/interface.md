# Interface

Import `mcr` or `mcr.interface` to use `mcr::options::Interface`. The option follows
cpr's `include/cpr/interface.h` and derives from `StringHolder<Interface>`.

```cpp
import std;
import mcr.interface;

mcr::options::Interface automatic;
mcr::options::Interface device{ "eth0" };
mcr::options::Interface from_view{ std::string_view{ "eth0" } };
mcr::options::Interface fragments{ "if!", "eth", "0" };
std::println("{}", device.Str());
```

Construction owns the supplied text without validation, normalization, or
interface lookup. The default value and empty strings represent no explicit
interface selection. Constructors accept a string, string view, C string,
pointer and length, or list of string fragments. Views and byte ranges respect
their length and retain embedded nulls; fragments are joined without separators.
Single-input constructors retain cpr's implicit conversions.

The option inherits `Str()`, `CStr()`, `Data()`, explicit string conversion,
comparisons, stream output, and concatenation returning another `Interface`.
Copying owns an independent value; moving is `noexcept`. API differences from
cpr are the C++23 module and namespace, plus the existing StringHolder accessor
names and fixes described in [types.md](types.md).

In cpr, `Session::SetInterface` passes a nonempty selector to `CURLOPT_INTERFACE`
and passes null for an empty selector. Its `test/get_tests.cpp` covers empty
C-string and string-view options. This project currently provides the option
value; Session integration and actual connection binding are not implemented.

Run `mcpp build` and `mcpp test` to verify construction, ownership, empty values,
binary ranges, copying, moving, and inherited operations.
