/**
 * @file redirect.cppm
 * @brief Redirect limits, credential forwarding, and POST method preservation options.
 */
export module mcr.redirect;

import std;

export namespace mcr::options {

    /**
     * @brief Flags selecting which redirects preserve POST, following cpr's bit values.
     * @note Bit operations and any() also support constant evaluation and do not throw.
     */
    enum class PostRedirectFlags : std::uint8_t {
        POST_301 = 0x1 << 0,                       ///< Preserve POST after a 301 redirect.
        POST_302 = 0x1 << 1,                       ///< Preserve POST after a 302 redirect.
        POST_303 = 0x1 << 2,                       ///< Preserve POST after a 303 redirect.
        POST_ALL = POST_301 | POST_302 | POST_303, ///< Preserve POST for all three redirect statuses.
        NONE     = 0x0                             ///< Use the default POST redirect behavior.
    };

    /**
     * @brief Combine the bits of two flag values.
     * @param lhs First flag value.
     * @param rhs Second flag value.
     * @return Bits set in either operand.
     */
    [[nodiscard]] constexpr auto operator|(PostRedirectFlags lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags {
        return static_cast<PostRedirectFlags>(std::to_underlying(lhs) | std::to_underlying(rhs));
    }

    /**
     * @brief Select the bits shared by two flag values.
     * @param lhs First flag value.
     * @param rhs Second flag value.
     * @return Bits set in both operands.
     */
    [[nodiscard]] constexpr auto operator&(PostRedirectFlags lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags {
        return static_cast<PostRedirectFlags>(std::to_underlying(lhs) & std::to_underlying(rhs));
    }

    /**
     * @brief Select the bits that differ between two flag values.
     * @param lhs First flag value.
     * @param rhs Second flag value.
     * @return Bits set in exactly one operand.
     */
    [[nodiscard]] constexpr auto operator^(PostRedirectFlags lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags {
        return static_cast<PostRedirectFlags>(std::to_underlying(lhs) ^ std::to_underlying(rhs));
    }

    /**
     * @brief Invert all eight bits, including unnamed bits, as in cpr.
     * @param flag Flag value to invert.
     * @return The full uint8_t complement, without masking to POST_ALL.
     */
    [[nodiscard]] constexpr auto operator~(PostRedirectFlags flag) noexcept -> PostRedirectFlags {
        return static_cast<PostRedirectFlags>(~std::to_underlying(flag));
    }

    /**
     * @brief Add the right operand's bits to the left operand.
     * @param lhs Flag value to update.
     * @param rhs Bits to add.
     * @return A reference to lhs after the update.
     */
    constexpr auto operator|=(PostRedirectFlags& lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags& {
        return lhs = lhs | rhs;
    }

    /**
     * @brief Keep only the left operand's bits selected by the right operand.
     * @param lhs Flag value to update.
     * @param rhs Bits to retain.
     * @return A reference to lhs after the update.
     */
    constexpr auto operator&=(PostRedirectFlags& lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags& {
        return lhs = lhs & rhs;
    }

    /**
     * @brief Toggle the right operand's bits in the left operand.
     * @param lhs Flag value to update.
     * @param rhs Bits to toggle.
     * @return A reference to lhs after the update.
     */
    constexpr auto operator^=(PostRedirectFlags& lhs, PostRedirectFlags rhs) noexcept -> PostRedirectFlags& {
        return lhs = lhs ^ rhs;
    }

    /**
     * @brief Check whether any bit is set, including unnamed bits.
     * @param flag Flag value to inspect.
     * @return True when flag differs from NONE.
     */
    [[nodiscard]] constexpr auto any(PostRedirectFlags flag) noexcept -> bool {
        return flag != PostRedirectFlags::NONE;
    }

    /**
     * @brief Store redirect options with cpr-compatible defaults and constructor overloads.
     * @note Values are stored without validation; maximum uses long to match curl's argument.
     */
    class Redirect {
    public:
        long              maximum{ 50L };                        ///< Maximum redirects to follow; zero refuses redirects and -1 means unlimited.
        bool              follow{ true };                        ///< Whether to follow 3xx redirect responses.
        bool              cont_send_cred{ false };               ///< Whether to continue sending authentication credentials when the hostname changes.
        PostRedirectFlags post_flags{ PostRedirectFlags::NONE }; ///< Which redirect statuses preserve POST.

        /**
         * @brief Follow up to 50 redirects with default credential and POST handling.
         */
        Redirect() = default;

        /**
         * @brief Construct a complete set of redirect options.
         * @param maximum_param Maximum redirects to follow, including zero and -1.
         * @param follow_param Whether to follow redirects.
         * @param cont_send_cred_param Whether to forward authentication credentials across hostnames.
         * @param post_flags_param Redirect statuses for which to preserve POST.
         */
        Redirect(
            long              maximum_param,
            bool              follow_param,
            bool              cont_send_cred_param,
            PostRedirectFlags post_flags_param
        ) : maximum{ maximum_param },
            follow{ follow_param },
            cont_send_cred{ cont_send_cred_param },
            post_flags{ post_flags_param } {}

        /**
         * @brief Set the redirect limit while keeping the remaining defaults.
         * @param maximum_param Maximum redirects to follow, including zero and -1.
         */
        explicit Redirect(long maximum_param) : maximum{ maximum_param } {}

        /**
         * @brief Set whether redirects are followed while keeping the remaining defaults.
         * @param follow_param Whether to follow redirects.
         */
        explicit Redirect(bool follow_param) : follow{ follow_param } {}

        /**
         * @brief Set redirect following and credential forwarding with default limit and POST handling.
         * @param follow_param Whether to follow redirects.
         * @param cont_send_cred_param Whether to forward authentication credentials across hostnames.
         */
        Redirect(bool follow_param, bool cont_send_cred_param)
            : follow{ follow_param }, cont_send_cred{ cont_send_cred_param } {}

        /**
         * @brief Set POST preservation flags while keeping the remaining defaults.
         * @param post_flags_param Redirect statuses for which to preserve POST.
         */
        explicit Redirect(PostRedirectFlags post_flags_param) : post_flags{ post_flags_param } {}
    };

} // namespace mcr::options
