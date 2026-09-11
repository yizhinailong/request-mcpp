/**
 * @file user_agent.cppm
 * @brief Owned User-Agent text for HTTP requests.
 */
export module mcr.user_agent;

export import mcr.types;

import std;

export namespace mcr {

    /**
     * @brief An owned User-Agent string; construction does not validate or modify the text.
     */
    class UserAgent : public StringHolder<UserAgent> {
    public:
        /**
         * @brief Construct an empty User-Agent.
         */
        UserAgent() = default;

        /**
         * @brief Construct a User-Agent by taking ownership of a string.
         * @param user_agent User-Agent text to store.
         */
        UserAgent(std::string user_agent) : StringHolder<UserAgent>(std::move(user_agent)) {}

        /**
         * @brief Construct a User-Agent by copying a string view.
         * @param user_agent View of the User-Agent text to copy.
         */
        UserAgent(std::string_view user_agent) : StringHolder<UserAgent>(user_agent) {}

        /**
         * @brief Construct a User-Agent by copying a null-terminated string.
         * @param user_agent Pointer to valid null-terminated User-Agent text.
         */
        UserAgent(char const* user_agent) : StringHolder<UserAgent>(user_agent) {}

        /**
         * @brief Construct a User-Agent from a byte range.
         * @param str Pointer to at least len readable bytes.
         * @param len Number of bytes to copy, including any embedded null bytes.
         */
        UserAgent(char const* str, std::size_t len) : StringHolder<UserAgent>(str, len) {}

        /**
         * @brief Construct a User-Agent by joining fragments without separators.
         * @param args User-Agent fragments to append in order.
         */
        UserAgent(std::initializer_list<std::string> args) : StringHolder<UserAgent>(args) {}

        UserAgent(UserAgent const& other)                = default;
        UserAgent(UserAgent&& other) noexcept            = default;
        ~UserAgent() override                            = default;

        UserAgent& operator=(UserAgent const& other)     = default;
        UserAgent& operator=(UserAgent&& other) noexcept = default;
    };

} // namespace mcr
