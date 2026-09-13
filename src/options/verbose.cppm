/**
 * @file verbose.cppm
 * @brief Option controlling verbose transfer diagnostics.
 */
export module mcr.verbose;

export namespace mcr::options {

    /**
     * @brief Enable or disable verbose transfer diagnostics, following cpr's Verbose interface.
     */
    class Verbose {
    public:
        /**
         * @brief Construct an option that enables verbose diagnostics.
         */
        Verbose() = default;

        /**
         * @brief Construct an option with the requested verbosity.
         * @param enabled Whether to enable verbose diagnostics.
         */
        Verbose(bool enabled) : verbose{ enabled } {}

        bool verbose{ true }; ///< Whether verbose diagnostics are enabled; defaults to true.
    };

} // namespace mcr::options
