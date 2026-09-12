/**
 * @file http_version.cppm
 * @brief HTTP protocol preferences available for the curl headers used to build the module.
 */
module;

#include <curl/curlver.h>

export module mcr.http_version;

import std;

export namespace mcr {

    /**
     * @brief Select an HTTP protocol policy using cpr's enum names and ordinal values.
     * @note These values require mapping to CURL_HTTP_VERSION_* before use with libcurl.
     * Availability depends on the build headers, not the runtime backend's protocol support.
     */
    enum class HttpVersionCode : std::uint8_t {
        VERSION_NONE,               ///< Let libcurl choose the protocol version.
        VERSION_1_0,                ///< Request HTTP/1.0.
        VERSION_1_1,                ///< Request HTTP/1.1.
#if LIBCURL_VERSION_NUM >= 0x072100 // 7.33.0
        VERSION_2_0,                ///< Attempt HTTP/2 with fallback to HTTP/1.1 if negotiation fails.
#endif
#if LIBCURL_VERSION_NUM >= 0x072F00 // 7.47.0
        VERSION_2_0_TLS,            ///< Attempt HTTP/2 for HTTPS with HTTP/1.1 fallback; use HTTP/1.1 for plain HTTP.
#endif
#if LIBCURL_VERSION_NUM >= 0x073100 // 7.49.0
        /**
         * @brief Use HTTP/2 directly for plain HTTP, without HTTP/1.1 Upgrade.
         * @note Requires server support. HTTPS negotiates with ALPN; since curl 8.10.0,
         * only HTTP/2 is offered for HTTPS with this policy.
         */
        VERSION_2_0_PRIOR_KNOWLEDGE,
#endif
#if LIBCURL_VERSION_NUM >= 0x074200 // 7.66.0
        VERSION_3_0,                ///< Attempt HTTP/3 with fallback to earlier HTTP versions.
#endif
#if LIBCURL_VERSION_NUM >= 0x075800 // 7.88.0
        VERSION_3_0_ONLY,           ///< Attempt HTTP/3 without falling back to earlier HTTP versions.
#endif
    };

    /**
     * @brief Store an HTTP protocol preference, defaulting to libcurl's choice.
     * @note The option stores its code verbatim; it does not validate or apply it to a request.
     */
    class HttpVersion {
    public:
        HttpVersionCode code{ HttpVersionCode::VERSION_NONE }; ///< Publicly mutable protocol preference.

        /** @brief Let libcurl choose the HTTP protocol version. */
        HttpVersion() = default;

        /**
         * @brief Store a protocol preference without implicit conversion from the enum.
         * @param code_param Protocol policy, preserved without validation.
         */
        constexpr explicit HttpVersion(HttpVersionCode code_param) noexcept : code{ code_param } {}
    };

} // namespace mcr
