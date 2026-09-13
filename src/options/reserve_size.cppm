/**
 * @file reserve_size.cppm
 * @brief Option specifying the response string's reserved capacity.
 */
export module mcr.reserve_size;

import std;

export namespace mcr {

    /**
     * @brief Store a response capacity hint, following cpr's ReserveSize interface.
     * @note Construction only stores the size; it does not allocate memory or validate capacity.
     */
    class ReserveSize {
    public:
        /**
         * @brief Construct an option with the requested response capacity.
         * @param size_param Number of bytes to reserve, including zero.
         */
        ReserveSize(std::size_t size_param) : size{ size_param } {}

        std::size_t size{ 0 }; ///< Requested response string capacity in bytes.
    };

} // namespace mcr
