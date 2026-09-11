/**
 * @file resolve.cppm
 * @brief An owned hostname-to-address override for a set of connection ports.
 */
export module mcr.resolve;

import std;

export namespace mcr {

    /**
     * @brief Store a custom address mapping, following cpr's Resolve option.
     * @note Construction stores text verbatim without address validation or DNS resolution.
     */
    class Resolve {
    public:
        std::string             host;  ///< Hostname to override.
        std::string             addr;  ///< Address text associated with the hostname.
        std::set<std::uint16_t> ports; ///< Ports to which the mapping applies.

        /**
         * @brief Take ownership of a mapping, using ports 80 and 443 when none are supplied.
         * @param host_param Hostname to override, stored without normalization.
         * @param addr_param Address text to store without validation.
         * @param ports_param Target ports; an omitted or empty set selects both 80 and 443.
         * @note Default ports are inserted only during this constructor, not after public field updates.
         */
        Resolve(
            std::string             host_param,
            std::string             addr_param,
            std::set<std::uint16_t> ports_param = { 80U, 443U }
        ) : host{ std::move(host_param) }, addr{ std::move(addr_param) }, ports{ std::move(ports_param) } {
            if (ports.empty()) {
                ports = { 80U, 443U };
            }
        }
    };

} // namespace mcr
