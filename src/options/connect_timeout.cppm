/**
 * @file connect_timeout.cppm
 * @brief Connection-phase timeout option built on the common timeout representation.
 */
export module mcr.connect_timeout;

export import mcr.timeout;

import std;

export namespace mcr {

    /**
     * @brief Distinguish connection timeouts from overall request timeouts, following cpr.
     * @note Inherits public ms storage and checked Milliseconds() conversion from Timeout.
     * When applied to CURLOPT_CONNECTTIMEOUT_MS, zero selects curl's default connection timeout.
     */
    class ConnectTimeout : public Timeout {
    public:
        /**
         * @brief Construct a connection timeout from a whole-millisecond duration.
         * @param duration Millisecond count to preserve, including zero and negative values.
         */
        ConnectTimeout(std::chrono::milliseconds const& duration) : Timeout{ duration } {}

        /**
         * @brief Implicitly construct a connection timeout from an integer millisecond count.
         * @param milliseconds Milliseconds to forward to the base timeout.
         */
        ConnectTimeout(std::int32_t milliseconds) : Timeout{ milliseconds } {}
    };

} // namespace mcr
