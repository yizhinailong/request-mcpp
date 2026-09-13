/**
 * @file accept_encoding.cppm
 * @brief Owned, deduplicated content encoding preferences for HTTP responses.
 */
export module mcr.accept_encoding;

import std;

export namespace mcr {

    /**
     * @brief Built-in encoding names and the disabled sentinel, retaining cpr's enum names and values.
     */
    enum class AcceptEncodingMethods : std::uint8_t {
        identity, ///< Request an unencoded response.
        deflate,  ///< Request deflate encoding.
        zlib,     ///< Request the literal zlib encoding name, as in cpr.
        gzip,     ///< Request gzip encoding.
        disabled, ///< Disable automatic Accept-Encoding handling.
    };

    /**
     * @brief Map built-in methods to their literal encoding names.
     * @note Exposes cpr's AcceptEncodingMethodsStringMap using the project's constant naming convention.
     */
    inline std::map<AcceptEncodingMethods, std::string> const ACCEPT_ENCODING_METHODS_STRING_MAP{
        { AcceptEncodingMethods::identity, "identity" },
        {  AcceptEncodingMethods::deflate,  "deflate" },
        {     AcceptEncodingMethods::zlib,     "zlib" },
        {     AcceptEncodingMethods::gzip,     "gzip" },
        { AcceptEncodingMethods::disabled, "disabled" },
    };

    /**
     * @brief Store encoding preferences with cpr's deduplication and disabled-sentinel semantics.
     * @note Public queries use Empty(), GetString(), and Disabled() following the project's naming style.
     * Custom strings are stored verbatim and output order is unspecified, as in cpr.
     */
    class AcceptEncoding {
    private:
        std::unordered_set<std::string> m_methods; ///< Owned encoding names, deduplicated by exact string equality.

    public:
        /**
         * @brief Construct an empty option representing all encodings supported by curl.
         */
        AcceptEncoding() = default;

        /**
         * @brief Store the names of the selected built-in encodings.
         * @param methods Built-in encodings; duplicates are ignored.
         * @throws std::out_of_range If a method is not a defined AcceptEncodingMethods value.
         * @note Mixing disabled with another method is checked by Disabled(), not during construction.
         */
        AcceptEncoding(std::initializer_list<AcceptEncodingMethods> const& methods) {
            for (auto const method : methods) {
                m_methods.insert(ACCEPT_ENCODING_METHODS_STRING_MAP.at(method));
            }
        }

        /**
         * @brief Copy custom encoding names without normalization or validation.
         * @param methods Encoding names, including empty strings; exact duplicates are ignored.
         * @note The exact string "disabled" has the same sentinel meaning as the enum value.
         */
        AcceptEncoding(std::initializer_list<std::string> const& methods) : m_methods{ methods } {}

        /**
         * @brief Check whether no encoding names were supplied.
         * @return True for an empty set, which selects curl's supported encodings rather than disabling them.
         */
        [[nodiscard]] auto Empty() const noexcept -> bool {
            return m_methods.empty();
        }

        /**
         * @brief Join encoding names with a comma and a space in unspecified order.
         * @return An owned string, or an empty string when no methods are stored.
         * @note Unlike cpr's getString(), this function safely handles an empty set.
         * An empty stored name still contributes an element and any required separator.
         */
        [[nodiscard]] auto GetString() const -> std::string {
            std::string      result;
            std::string_view separator;
            for (auto const& method : m_methods) {
                result    += separator;
                result    += method;
                separator  = ", ";
            }
            return result;
        }

        /**
         * @brief Check whether automatic Accept-Encoding handling is disabled.
         * @return True if "disabled" is the only distinct name; false if it is absent.
         * @throws std::invalid_argument If "disabled" is combined with any other distinct name.
         */
        [[nodiscard]] auto Disabled() const -> bool {
            if (!m_methods.contains(ACCEPT_ENCODING_METHODS_STRING_MAP.at(AcceptEncodingMethods::disabled))) {
                return false;
            }
            if (m_methods.size() != 1) {
                throw std::invalid_argument{ "AcceptEncoding does not accept any other values if 'disabled' is present. You set the following encodings: " + GetString() };
            }
            return true;
        }
    };

} // namespace mcr
