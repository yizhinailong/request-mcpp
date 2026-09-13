/**
 * @file local_port.cppm
 * @brief Local source port option with an implicit uint16_t conversion.
 */
export module mcr.local_port;

import std;

export namespace mcr::options {

    /**
     * @brief Store a local source port, following cpr's LocalPort option.
     * @note Construction stores the supplied uint16_t value without validation or socket binding.
     */
    class LocalPort {
    private:
        std::uint16_t m_local_port; ///< Stored local port, including zero.

    public:
        /**
         * @brief Implicitly construct an option from a local port number.
         * @param local_port Port value to preserve without modification.
         */
        LocalPort(std::uint16_t local_port) : m_local_port{ local_port } {}

        /**
         * @brief Implicitly retrieve the stored port number.
         * @return The original uint16_t value.
         */
        [[nodiscard]] operator std::uint16_t() const {
            return m_local_port;
        }
    };

} // namespace mcr::options
