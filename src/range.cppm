/**
 * @file range.cppm
 * @brief Single and multiple transfer ranges formatted for curl.
 */
export module mcr.range;

import std;

export namespace mcr {

    /**
     * @brief Store a transfer range, following cpr's optional endpoint defaults.
     * @note Negative endpoints are retained in storage and omitted from the formatted text.
     * Endpoint order and protocol validity are not checked.
     */
    class Range {
    public:
        /**
         * @brief Construct a range with optional start and finish positions.
         * @param resume_from_param Starting position, defaulting to zero when absent.
         * @param finish_at_param Ending position, defaulting to -1 when absent.
         */
        explicit Range(std::optional<std::int64_t> resume_from_param = std::nullopt, std::optional<std::int64_t> finish_at_param = std::nullopt)
            : resume_from{ resume_from_param.value_or(0) }, finish_at{ finish_at_param.value_or(-1) } {}

        std::int64_t resume_from; ///< Publicly mutable starting position; any negative value omits the start.
        std::int64_t finish_at;   ///< Publicly mutable ending position; any negative value omits the finish.

        /**
         * @brief Format the current endpoints as from-to, omitting negative endpoint numbers.
         * @return Owned text such as "0-", "2-3", "-500", or "-", without a bytes= prefix.
         */
        [[nodiscard]] auto Str() const -> std::string {
            std::string result;
            if (resume_from >= 0) {
                result = std::to_string(resume_from);
            }
            result += '-';
            if (finish_at >= 0) {
                result += std::to_string(finish_at);
            }
            return result;
        }
    };

    /**
     * @brief Own an ordered sequence of ranges, following cpr's MultiRange interface.
     * @note Ranges are copied without sorting, merging, deduplication, or validation.
     */
    class MultiRange {
    private:
        std::vector<Range> m_ranges; ///< Owned range values in the supplied order.

    public:
        /**
         * @brief Copy a list of ranges, including an empty list.
         * @param ranges Range values to retain in order.
         */
        MultiRange(std::initializer_list<Range> ranges) : m_ranges{ ranges } {}

        /**
         * @brief Join the stored range strings with a comma and a space.
         * @return Owned text with no trailing separator, or an empty string for an empty list.
         */
        [[nodiscard]] auto Str() const -> std::string {
            std::string      result;
            std::string_view separator;
            for (auto const& range : m_ranges) {
                result    += separator;
                result    += range.Str();
                separator  = ", ";
            }
            return result;
        }
    };

} // namespace mcr
