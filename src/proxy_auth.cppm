/**
 * @file proxy_auth.cppm
 * @brief Encoded proxy credentials and protocol-specific authentication maps.
 */
export module mcr.proxy_auth;
export import mcr.secure_string;
import mcr.util;
import std;

export namespace mcr {
    /** @brief Own percent-encoded proxy credentials, preserving cpr's credential accessors. */
    class EncodedAuthentication {
    private:
        util::SecureString m_username; ///< Encoded username.
        util::SecureString m_password; ///< Encoded password.

    public:
        EncodedAuthentication() = default;

        /** @brief Encode and own credentials. @param username Raw username. @param password Raw password. */
        EncodedAuthentication(std::string_view username, std::string_view password)
            : m_username{ util::url_encode(username) }, m_password{ util::url_encode(password) } {}

        virtual ~EncodedAuthentication()                                           = default;
        EncodedAuthentication(EncodedAuthentication const&)                        = default;
        EncodedAuthentication(EncodedAuthentication&&) noexcept                    = default;
        auto operator=(EncodedAuthentication const&) -> EncodedAuthentication&     = default;
        auto operator=(EncodedAuthentication&&) noexcept -> EncodedAuthentication& = default;

        /** @brief Borrow the encoded username. @return Bounded view of owned storage. */
        [[nodiscard]] auto GetUsername() const noexcept -> std::string_view { return m_username; }

        /** @brief Borrow the encoded password. @return Bounded view of owned storage. */
        [[nodiscard]] auto GetPassword() const noexcept -> std::string_view { return m_password; }

        /** @brief Borrow secure username storage. @return Encoded username. */
        [[nodiscard]] auto GetUsernameUnderlying() const noexcept -> util::SecureString const& { return m_username; }

        /** @brief Borrow secure password storage. @return Encoded password. */
        [[nodiscard]] auto GetPasswordUnderlying() const noexcept -> util::SecureString const& { return m_password; }
    };

    /** @brief Map exact protocol names to owned proxy credentials. */
    class ProxyAuthentication {
    private:
        std::map<std::string, EncodedAuthentication, std::less<>> m_auths; ///< Transparent protocol lookup.

    public:
        ProxyAuthentication() = default;

        /** @brief Copy protocol credentials. @param auths Entries to own. */
        ProxyAuthentication(std::initializer_list<std::pair<std::string const, EncodedAuthentication>> auths) : m_auths{ auths } {}

        /** @brief Copy a standard map. @param auths Entries to own. */
        explicit ProxyAuthentication(std::map<std::string, EncodedAuthentication> const& auths) : m_auths{ auths.begin(), auths.end() } {}

        /** @brief Inspect membership without insertion. @param protocol Exact protocol. @return Whether configured. */
        [[nodiscard]] auto Has(std::string_view protocol) const -> bool { return m_auths.contains(protocol); }

        /** @brief Borrow encoded username, inserting an empty entry when absent. @param protocol Exact protocol. @return Encoded username. */
        [[nodiscard]] auto GetUsername(std::string_view protocol) -> std::string_view { return m_auths[std::string{ protocol }].GetUsername(); }

        /** @brief Borrow encoded password, inserting an empty entry when absent. @param protocol Exact protocol. @return Encoded password. */
        [[nodiscard]] auto GetPassword(std::string_view protocol) -> std::string_view { return m_auths[std::string{ protocol }].GetPassword(); }

        /** @brief Borrow existing secure username storage. @param protocol Exact protocol. @return Encoded username. @throws std::out_of_range If absent. */
        [[nodiscard]] auto GetUsernameUnderlying(std::string_view protocol) const -> util::SecureString const& { return find(protocol).GetUsernameUnderlying(); }

        /** @brief Borrow existing secure password storage. @param protocol Exact protocol. @return Encoded password. @throws std::out_of_range If absent. */
        [[nodiscard]] auto GetPasswordUnderlying(std::string_view protocol) const -> util::SecureString const& { return find(protocol).GetPasswordUnderlying(); }

    private:
        auto find(std::string_view protocol) const -> EncodedAuthentication const& {
            auto const entry{ m_auths.find(protocol) };
            if (entry == m_auths.end()) {
                throw std::out_of_range{ "mcr::ProxyAuthentication: protocol not configured." };
            }
            return entry->second;
        }
    };
} // namespace mcr
