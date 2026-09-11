/**
 * @file unix_socket.cppm
 * @brief Owned Unix domain socket path for HTTP connections.
 */
export module mr.unix_socket;

import std;

export namespace mcr {

    /**
     * @brief An immutable Unix domain socket path, following cpr's UnixSocket interface.
     */
    class UnixSocket {
    private:
        std::string const m_unix_socket; ///< Owned socket path, immutable after construction.

    public:
        /**
         * @brief Take ownership of a Unix domain socket path.
         * @param unix_socket Socket path to store without validation.
         */
        UnixSocket(std::string unix_socket) : m_unix_socket{ std::move(unix_socket) } {}

        /**
         * @brief Get the stored socket path as a null-terminated string.
         * @return A pointer to the owned path, valid for this object's lifetime.
         */
        [[nodiscard]] auto GetUnixSocketString() const noexcept -> char const* {
            return m_unix_socket.data();
        }
    };

} // namespace mcr
