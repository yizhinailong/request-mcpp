/**
 * @file ssl_ctx.cppm
 * @brief OpenSSL-specific CA loading callback for advanced curl use.
 */
module;
#include <curl/curl.h>
export module mcr.ssl_ctx;

export namespace mcr {
#ifdef MCR_SSL_CTX_OPENSSL
    inline constexpr bool SSL_CTX_OPENSSL_ENABLED{ true }; ///< This build can load CA certificates into OpenSSL contexts.
#else
    inline constexpr bool SSL_CTX_OPENSSL_ENABLED{ false }; ///< Schannel builds use ssl::CaBuffer instead.
#endif

    /**
     * @brief Load a PEM CA bundle into the OpenSSL context supplied to a curl SSL_CTX callback.
     * @param curl Curl easy handle; unused, as in cpr.
     * @param sslctx OpenSSL SSL_CTX from the same OpenSSL library linked to mcr.
     * @param raw_cert_buf Borrowed NUL-terminated PEM bundle, alive throughout the callback.
     * @return CURLE_OK after loading at least one certificate, CURLE_ABORTED_BY_CALLBACK for
     * invalid arguments or malformed certificates, CURLE_OUT_OF_MEMORY on allocation failure,
     * or CURLE_NOT_BUILT_IN when OpenSSL context support is absent.
     * @note Configure CURLOPT_SSL_CTX_FUNCTION and CURLOPT_SSL_CTX_DATA together. This callback
     * never throws or owns the supplied pointers. Prefer Ssl(CaBuffer{...}) for portable CA loading.
     */
    auto sslctx_function_load_ca_cert_from_buffer(CURL* curl, void* sslctx, void* raw_cert_buf) noexcept -> CURLcode;
} // namespace mcr
