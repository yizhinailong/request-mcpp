/**
 * @file auth.cppm
 * @brief Owned username/password credentials and HTTP authentication mode selection.
 */
export module mcr.auth;

import mcr.secure_string;

import std;

export namespace mcr {

    /** @brief Authentication policies retaining cpr's uint8_t names and ordinal values. */
    enum class AuthMode : std::uint8_t {
        BASIC,     ///< HTTP Basic authentication.
        DIGEST,    ///< HTTP Digest authentication.
        NTLM,      ///< NTLM authentication.
        NEGOTIATE, ///< Negotiate/SPNEGO authentication.
        ANY,       ///< Let curl choose among the authentication methods it supports.
        ANYSAFE,   ///< Let curl choose among supported methods other than Basic.
    };

    /**
     * @brief Own raw username:password bytes and an authentication policy, following cpr.
     * @note Uses util::SecureString storage; its allocator wipes released heap allocations.
     * Credentials and mode are retained without encoding, normalization, or validation.
     */
    class Authentication {
    private:
        util::SecureString m_auth_string; ///< Owned credentials, including the colon separator.
        AuthMode           m_auth_mode;   ///< Selected authentication policy.

    public:
        /**
         * @brief Copy the input views with one colon inserted between them.
         * @param username Username bytes; the view need not be null-terminated.
         * @param password Password bytes; empty values and embedded nulls are retained.
         * @param auth_mode Authentication policy to store verbatim.
         * @throws std::bad_alloc If allocating credential storage fails.
         * @throws std::length_error If the combined credentials exceed string capacity.
         */
        Authentication(std::string_view username, std::string_view password, AuthMode auth_mode)
            : m_auth_string{ username }, m_auth_mode{ auth_mode } {
            m_auth_string += ':';
            m_auth_string += password;
        }

        /**
         * @brief Borrow the null-terminated credential string.
         * @return A nonnull pointer to owned bytes; C-string consumers stop at the first embedded null.
         * @note Assignment, moving, or destruction can invalidate the returned pointer.
         */
        [[nodiscard]] auto GetAuthString() const noexcept -> char const* {
            return m_auth_string.c_str();
        }

        /**
         * @brief Read the selected authentication policy.
         * @return The mode supplied at construction or copied from another authentication object.
         */
        [[nodiscard]] auto GetAuthMode() const noexcept -> AuthMode {
            return m_auth_mode;
        }
    };

} // namespace mcr
