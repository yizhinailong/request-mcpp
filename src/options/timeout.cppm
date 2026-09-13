/**
 * @file timeout.cppm
 * @brief Request timeout durations with checked conversion to curl's millisecond argument.
 */
export module mcr.timeout;

import std;

export namespace mcr::options {

    /**
     * @brief A request timeout stored in milliseconds, following cpr's Timeout interface.
     * @note Zero disables the overall request timeout when supplied to curl.
     * Negative values are preserved; this type does not validate curl option semantics.
     */
    class Timeout {
    public:
        /**
         * @brief Convert a chrono duration to whole milliseconds, truncating toward zero.
         * @tparam Rep Source duration's representation type.
         * @tparam Period Source duration's tick period.
         * @param duration Duration to convert with std::chrono::duration_cast.
         * @pre The duration and conversion arithmetic must be representable by duration_cast.
         * Floating-point durations must be finite and convert within the milliseconds representation.
         */
        template <typename Rep, typename Period>
        Timeout(std::chrono::duration<Rep, Period> const& duration)
            : ms{ std::chrono::duration_cast<std::chrono::milliseconds>(duration) } {}

        /**
         * @brief Construct a timeout from an integer millisecond count.
         * @param milliseconds Milliseconds to store, including zero or negative values.
         */
        Timeout(std::int32_t milliseconds) : Timeout{ std::chrono::milliseconds{ milliseconds } } {}

        /**
         * @brief Convert the stored duration to the long argument required by curl.
         * @return The stored millisecond count without narrowing loss.
         * @throws std::overflow_error If the count exceeds the maximum long value.
         * @throws std::underflow_error If the count is below the minimum long value.
         */
        [[nodiscard]] auto Milliseconds() const -> long {
            auto const count{ ms.count() };
            if (std::cmp_greater(count, (std::numeric_limits<long>::max)())) {
                throw std::overflow_error{ std::format("mcr::options::Timeout: timeout value overflow: {} ms.", count) };
            }
            if (std::cmp_less(count, (std::numeric_limits<long>::min)())) {
                throw std::underflow_error{ std::format("mcr::options::Timeout: timeout value underflow: {} ms.", count) };
            }
            return static_cast<long>(count);
        }

        std::chrono::milliseconds ms; ///< Stored duration, publicly mutable as in cpr.
    };

} // namespace mcr::options
