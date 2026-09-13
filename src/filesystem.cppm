/**
 * @file filesystem.cppm
 * @brief Standard filesystem namespace for the C++23 API.
 */
export module mcr.filesystem;
import std;

export namespace mcr {
    namespace fs = std::filesystem; ///< C++23 always provides the standard filesystem API.
}
