/// Error reporting for the whole implementation.
///
/// Everything fallible returns `Result<T>` rather than throwing: a bad cube
/// string and an unsolvable arrangement are ordinary outcomes of user input,
/// not exceptional ones, and `std::expected` keeps them in the signature where
/// a caller cannot ignore them. Exceptions are left for what they are good at
/// (allocation failure), so the solvers stay free of try blocks.
#pragma once

#include <expected>
#include <string>
#include <utility>

namespace rubik {

enum class ErrorKind {
    /// The 24 letters are not a well-formed cube string.
    invalid_state,
    /// A well-formed cube that no sequence of turns can reach from solved.
    unsolvable,
    /// Bad command line, missing file, and friends.
    usage,
};

struct Error {
    ErrorKind kind;
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorKind kind, std::string message) {
    return std::unexpected(Error{kind, std::move(message)});
}

}  // namespace rubik
