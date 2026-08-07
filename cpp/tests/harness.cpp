#include "harness.hpp"

#include <chrono>
#include <exception>
#include <print>

namespace testing {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

int add(std::string_view name, Body body) {
    registry().push_back({name, body});
    return 0;
}

void fail(const std::string& message, std::string_view file, int line) {
    throw Failure{std::format("{}:{}: {}", file, line, message)};
}

int run_all() {
    int failures = 0;
    const auto started = std::chrono::steady_clock::now();

    for (const Case& test : registry()) {
        try {
            test.body();
            std::println("  ok    {}", test.name);
        } catch (const Failure& failure) {
            ++failures;
            std::println("  FAIL  {}\n        {}", test.name, failure.what());
        } catch (const std::exception& error) {
            ++failures;
            std::println("  ERROR {}\n        unexpected exception: {}", test.name, error.what());
        }
    }

    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::println("\n{} of {} tests passed in {:.2f} s",
                 registry().size() - static_cast<std::size_t>(failures), registry().size(),
                 seconds);
    return failures == 0 ? 0 : 1;
}

}  // namespace testing

int main() { return testing::run_all(); }
