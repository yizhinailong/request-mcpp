/**
 * @file local_port_range.cppm
 * @brief Local source port range option with an implicit uint16_t conversion.
 */
export module mcr.local_port_range;

import std;

export namespace mcr {

    /**
     * @brief Store a local port range value, following cpr's LocalPortRange option.
     * @note Construction stores the supplied uint16_t value without validation or port probing.
     */
    class LocalPortRange {
    private:
        std::uint16_t m_local_port_range; ///< Stored local port range value, including zero.

    public:
        /**
         * @brief Implicitly construct an option from a local port range value.
         * @param local_port_range Range value to preserve without modification.
         */
        LocalPortRange(std::uint16_t local_port_range) : m_local_port_range{ local_port_range } {}

        /**
         * @brief Implicitly retrieve the stored port range value.
         * @return The original uint16_t value.
         */
        [[nodiscard]] operator std::uint16_t() const {
            return m_local_port_range;
        }
    };

} // namespace mcr
