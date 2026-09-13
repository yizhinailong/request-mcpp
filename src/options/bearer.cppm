/**
 * @file bearer.cppm
 * @brief Owned bearer tokens with cpr-compatible polymorphic access.
 */
module;

#include <curl/curlver.h>

export module mcr.bearer;

export import mcr.secure_string;

import std;

export namespace mcr::options {

#if LIBCURL_VERSION_NUM >= 0x073D00 // HTTP bearer authentication was added in 7.61.0.
    /**
     * @brief Own raw bearer token bytes and allow derived classes to customize token access.
     * @note Available when built with curl headers at least 7.61.0, following cpr.
     * No authorization prefix, encoding, or validation is applied to the token.
     */
    class Bearer {
    public:
        /**
         * @brief Copy a token view into owned secure-string storage.
         * @param token Token bytes, including empty views, without requiring null termination.
         * @throws std::bad_alloc If allocating token storage fails.
         * @throws std::length_error If the token exceeds string capacity.
         */
        Bearer(std::string_view token) : m_token_string{ token } {}

        /**
         * @brief Copy token bytes into independent secure-string storage.
         * @param other Source token.
         */
        Bearer(Bearer const& other)                        = default;

        /**
         * @brief Move the token using secure-string move semantics.
         * @param other Source token.
         */
        Bearer(Bearer&& other) noexcept                    = default;

        /**
         * @brief Destroy the token, including derived state when deleted through a base pointer.
         */
        virtual ~Bearer() noexcept                         = default;

        /**
         * @brief Move token storage.
         * @param other Source token.
         * @return This object after assignment.
         */
        auto operator=(Bearer&& other) noexcept -> Bearer& = default;

        /**
         * @brief Copy token bytes.
         * @param other Source token.
         * @return This object after assignment.
         */
        auto operator=(Bearer const& other) -> Bearer&     = default;

        /**
         * @brief Borrow the null-terminated token string; derived classes may override this accessor.
         * @return For the base implementation, a nonnull pointer into owned token storage.
         * @note Assignment, moving, derived mutation, or destruction can invalidate the pointer.
         * Embedded null bytes are stored, but C-string consumers see only their preceding prefix.
         */
        [[nodiscard]] virtual auto GetToken() const noexcept -> char const* {
            return m_token_string.c_str();
        }

    protected:
        utils::SecureString m_token_string; ///< Owned token bytes, available for derived customization.
    };
#endif

} // namespace mcr::options
