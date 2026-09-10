# Error handling

Import `mr.error` to use `mr::ErrorCode`, `mr::Error`, and
`mr::Result<T>` (`std::expected<T, mr::Error>`). `Result<void>` represents an
operation with no return value.

`mr::check_curl_error(curl_code, message)` returns a successful `Result<void>`
for `CURLE_OK` and `std::unexpected<mr::Error>` for every other curl status.
The message is owned by the error and preserved as supplied; it defaults to an
empty string. Pass the curl error buffer when a detailed diagnostic is available.

```cpp
import std;
import mr.error;

auto finish_transfer(std::int32_t curl_code, std::string diagnostic,
                     std::string body) -> mr::Result<std::string> {
    auto status = mr::check_curl_error(curl_code, std::move(diagnostic));
    if (!status) {
        return std::unexpected{std::move(status.error())};
    }
    return body;
}

// After checking !result, inspect result.error().code and result.error().message.
// mr::to_string(result.error().code) returns the symbolic error name.
```

Compatibility with cpr:

- Error code names and values, the default `ErrorCode::OK`, and
  `Error::operator bool()` are preserved. An `Error` is true when it describes a
  failure; a `Result<T>` is true when it contains a successful result.
- Curl codes are explicitly mapped using the declared curl dependency. They
  must not be cast to `ErrorCode`: for example, curl timeout is 28 while
  `ErrorCode::OPERATION_TIMEDOUT` is 18. Unsupported or unknown curl codes map
  to `UNKNOWN_ERROR`.
- The function-local static `std::unordered_map<ErrorCode, std::string>` is
  preserved. String conversion is `mr::to_string(code)` instead of adding an
  overload to `std`. As in cpr, `.at()` throws `std::out_of_range` for an invalid
  enum value; the defined `UNKNOWN_ERROR` has its own entry.
- `Error` constructors accept a message by value, allowing both copying and
  moving, and also accept `ErrorCode` directly. `Result<T>` and
  `check_curl_error()` provide the additional expected-based result interface.

Run `mcpp build` and `mcpp test` to validate the module and its error handling.
