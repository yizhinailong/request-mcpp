/**
 * @file ssl_ctx.cpp
 * @brief Parse PEM certificates with RAII before updating an OpenSSL trust store.
 */
module;
#include <curl/curl.h>
#ifdef MCR_SSL_CTX_OPENSSL
    #include <openssl/bio.h>
    #include <openssl/err.h>
    #include <openssl/pem.h>
    #include <openssl/ssl.h>
    #include <openssl/x509_vfy.h>
#endif
module mcr.ssl_ctx;
import std;

namespace mcr {
    auto sslctx_function_load_ca_cert_from_buffer(CURL* /*curl*/, void* sslctx, void* raw_cert_buf) noexcept -> CURLcode {
        if (!sslctx || !raw_cert_buf) {
            return CURLE_ABORTED_BY_CALLBACK;
        }
#ifdef MCR_SSL_CTX_OPENSSL
        try {
            auto const* buffer{ static_cast<char const*>(raw_cert_buf) };
            auto const  length{ std::char_traits<char>::length(buffer) };
            if (length > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
                return CURLE_ABORTED_BY_CALLBACK;
            }
            using Bio         = std::unique_ptr<BIO, decltype(&BIO_free)>;
            using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;
            Bio bio{ BIO_new_mem_buf(buffer, static_cast<int>(length)), &BIO_free };
            if (!bio) {
                return CURLE_OUT_OF_MEMORY;
            }
            auto* store{ SSL_CTX_get_cert_store(static_cast<SSL_CTX*>(sslctx)) };
            if (!store) {
                return CURLE_ABORTED_BY_CALLBACK;
            }
            std::vector<Certificate> certificates;
            while (true) {
                ERR_clear_error();
                Certificate certificate{ PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), &X509_free };
                if (!certificate) {
                    auto const error{ ERR_peek_last_error() };
                    if (error && !(ERR_GET_LIB(error) == ERR_LIB_PEM && ERR_GET_REASON(error) == PEM_R_NO_START_LINE)) {
                        return CURLE_ABORTED_BY_CALLBACK;
                    }
                    ERR_clear_error();
                    break;
                }
                certificates.push_back(std::move(certificate));
            }
            if (certificates.empty()) {
                return CURLE_ABORTED_BY_CALLBACK;
            }
            for (auto const& certificate : certificates) {
                if (X509_STORE_add_cert(store, certificate.get()) != 1) {
                    auto const error{ ERR_peek_last_error() };
                    if (ERR_GET_LIB(error) != ERR_LIB_X509 || ERR_GET_REASON(error) != X509_R_CERT_ALREADY_IN_HASH_TABLE) {
                        return CURLE_ABORTED_BY_CALLBACK;
                    }
                    ERR_clear_error();
                }
            }
            return CURLE_OK;
        } catch (std::bad_alloc const&) {
            return CURLE_OUT_OF_MEMORY;
        } catch (...) {
            return CURLE_ABORTED_BY_CALLBACK;
        }
#else
        return CURLE_NOT_BUILT_IN;
#endif
    }
} // namespace mcr
