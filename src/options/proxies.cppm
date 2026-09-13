/**
 * @file proxies.cppm
 * @brief Owned proxy addresses indexed by exact protocol names.
 */
export module mcr.proxies;

import std;

export namespace mcr::options {

    /**
     * @brief Store protocol-to-proxy mappings, following cpr's Proxies option.
     * @note Protocol names are case-sensitive and all text is stored without validation.
     * Has() only queries; subscript inserts an empty value when a protocol is absent.
     */
    class Proxies {
    private:
        std::map<std::string, std::string, std::less<>> m_hosts; ///< Owned mappings with transparent, case-sensitive lookup.

    public:
        /**
         * @brief Construct an option with no proxy mappings.
         */
        Proxies() = default;

        /**
         * @brief Copy protocol-to-proxy pairs from an initializer list.
         * @param hosts Mappings to store using std::map's duplicate-key initialization rules.
         */
        Proxies(std::initializer_list<std::pair<std::string const, std::string>> hosts)
            : m_hosts{ hosts } {}

        /**
         * @brief Explicitly copy protocol-to-proxy mappings from a standard map.
         * @param hosts Source map, which remains independent of this option.
         */
        explicit Proxies(std::map<std::string, std::string> const& hosts)
            : m_hosts{ hosts.begin(), hosts.end() } {}

        /**
         * @brief Check for an exactly matching protocol without inserting or copying the key.
         * @param protocol Borrowed protocol name; bounded views and embedded nulls are supported.
         * @return True when the key exists, even if its proxy address is empty.
         */
        [[nodiscard]] auto Has(std::string_view protocol) const -> bool {
            return m_hosts.contains(protocol);
        }

        /**
         * @brief Access a proxy address, inserting an empty string for an absent protocol.
         * @param protocol Protocol name to look up, copied into owned storage if inserted.
         * @return A read-only reference to the stored address, stable across other insertions.
         * @note This operation is non-const because lookup may insert, matching cpr.
         */
        auto operator[](std::string_view protocol) -> std::string const& {
            auto const found{ m_hosts.find(protocol) };
            if (found != m_hosts.end()) {
                return found->second;
            }
            return m_hosts.try_emplace(std::string{ protocol }).first->second;
        }
    };

} // namespace mcr::options
