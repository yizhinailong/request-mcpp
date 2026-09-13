/**
 * @file low_speed.cppm
 * @brief Transfer low-speed threshold and observation duration.
 */
export module mcr.low_speed;

import std;

export namespace mcr {

    /**
     * @brief Store the minimum transfer rate and duration used to detect a slow connection.
     * @note Values are stored verbatim, including zero and negative values, without validation.
     * The option follows cpr's chrono interface and omits its deprecated integer-time constructor.
     */
    class LowSpeed {
    public:
        /**
         * @brief Construct a low-speed option with an explicit duration in seconds.
         * @param limit_param Minimum transfer rate in bytes per second.
         * @param time_param Duration during which the transfer rate may remain below the limit.
         */
        LowSpeed(std::int32_t limit_param, std::chrono::seconds time_param)
            : limit{ limit_param }, time{ time_param } {}

        std::int32_t         limit; ///< Publicly mutable minimum transfer rate in bytes per second.
        std::chrono::seconds time;  ///< Publicly mutable observation duration in whole seconds.
    };

} // namespace mcr
