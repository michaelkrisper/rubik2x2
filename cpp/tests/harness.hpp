/// A minimal test harness: no external framework, because the implementation
/// takes no external dependencies and its tests should not either.
///
/// `TEST(name) { ... }` registers a case; `CHECK`, `CHECK_EQ` and `CHECK_NE`
/// abort the case they are in and let the rest run.
#pragma once

#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace testing {

struct Failure : std::exception {
    explicit Failure(std::string text) : text(std::move(text)) {}
    [[nodiscard]] const char* what() const noexcept override { return text.c_str(); }
    std::string text;
};

using Body = void (*)();

struct Case {
    std::string_view name;
    Body body;
};

std::vector<Case>& registry();
int add(std::string_view name, Body body);

/// Runs every registered case; returns the process exit code.
int run_all();

[[noreturn]] void fail(const std::string& message, std::string_view file, int line);

/// Values are printed when the standard library knows how; the expression text
/// is always shown, which is enough to find the case that broke.
template <typename T>
[[nodiscard]] std::string describe(const T& value) {
    if constexpr (std::formattable<const T&, char>) {
        return std::format("{}", value);
    } else {
        return "<not printable>";
    }
}

}  // namespace testing

#define TEST(name)                                                        \
    static void name();                                                   \
    [[maybe_unused]] static const int registered_##name = ::testing::add(#name, &name); \
    static void name()

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            ::testing::fail(std::format("CHECK({}) failed", #condition), __FILE__,    \
                            __LINE__);                                               \
        }                                                                            \
    } while (false)

#define CHECK_EQ(left, right)                                                              \
    do {                                                                                   \
        const auto check_left = (left);                                                   \
        const auto check_right = (right);                                                 \
        if (!(check_left == check_right)) {                                                \
            ::testing::fail(std::format("CHECK_EQ({}, {}) failed: {} != {}", #left, #right, \
                                        ::testing::describe(check_left),                   \
                                        ::testing::describe(check_right)),                 \
                            __FILE__, __LINE__);                                           \
        }                                                                                  \
    } while (false)

#define CHECK_NE(left, right)                                                              \
    do {                                                                                   \
        const auto check_left = (left);                                                   \
        const auto check_right = (right);                                                 \
        if (check_left == check_right) {                                                   \
            ::testing::fail(std::format("CHECK_NE({}, {}) failed: both {}", #left, #right,  \
                                        ::testing::describe(check_left)),                  \
                            __FILE__, __LINE__);                                           \
        }                                                                                  \
    } while (false)
