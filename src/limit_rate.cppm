/**
 * @file limit_rate.cppm
 * @brief Download and upload transfer rate limits in bytes per second.
 */
export module mcr.limit_rate;

import std;

export namespace mcr {

    /**
     * @brief Store independent download and upload limits, following cpr's LimitRate option.
     * @note Zero represents an unlimited rate when passed to curl. Negative values are
     * preserved without validation; constructing this option does not throttle a transfer.
     */
    class LimitRate {
    public:
        /**
         * @brief Construct an option with download and upload rate limits.
         * @param downrate_param Download limit in bytes per second, stored without modification.
         * @param uprate_param Upload limit in bytes per second, stored without modification.
         */
        LimitRate(std::int64_t downrate_param, std::int64_t uprate_param)
            : downrate{ downrate_param }, uprate{ uprate_param } {}

        std::int64_t downrate{ 0 }; ///< Publicly mutable download rate limit in bytes per second.
        std::int64_t uprate{ 0 };   ///< Publicly mutable upload rate limit in bytes per second.
    };

} // namespace mcr
