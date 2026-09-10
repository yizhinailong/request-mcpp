# Common request types

Import `mr` or `mr.cprtypes` to use the types in namespace `mcr`.

```cpp
import std;
import mr;

mcr::Url url{ "https://example.test", "/api" };
url += mcr::Url{ "/items" };
mcr::Header headers{ { "Content-Type", "application/json" } };
std::println("{}: {}", url.str(), headers.at("content-type"));
```

- `cpr_off_t` aliases the configured curl's `curl_off_t`. `cpr_pf_arg_t`
  aliases `cpr_off_t` for progress callbacks in the current curl dependency.
  Consumers do not need to include curl headers to use either alias.
- `StringHolder<T>` owns its text and returns `T` from concatenation.
  `Url` supports strings, string views, C strings, byte ranges, and lists of
  fragments. Views and ranges are copied; explicit lengths preserve embedded
  null bytes. URLs are stored without validation or encoding.
- `str()`, `c_str()`, `data()`, explicit conversion to `std::string`, stream
  output, copy/move operations, comparisons, and void-returning `+=` preserve
  cpr's interfaces. The protected `str_` member remains available to derived
  string option types.
- `Header` is a map using `CaseInsensitiveCompare`. Names compare without case;
  values and the spelling of the first inserted key remain unchanged. As in
  cpr, comparison uses `std::tolower` with unsigned bytes in the current C
  locale. Keep that locale stable while the map contains keys.

Differences from the referenced cpr implementation:

- Public types live in `mcr` and are exported through C++23 modules.
- Only the current curl dependency is supported; legacy progress callback
  argument types and curl version compatibility branches are omitted.
- `StringHolder<T>::operator+=(StringHolder<T> const&)` appends the other
  holder's stored string; cpr's attempt to append the holder itself does not
  compile when instantiated.
- `operator!=(char const*)` compares string contents, correcting cpr's pointer
  comparison so it agrees with `operator==` for independently stored strings.

Run `mcpp build` and `mcpp test` to validate the module and its regression tests.
