#include <cstdlib>
#include <expected>
#include <string_view>

int main() {
    const std::expected<int, std::string_view> success = 42;
    if ((!success.has_value()) || (success.value() != 42)) {
        return EXIT_FAILURE;
    }

    const std::expected<int, std::string_view> failure = std::unexpected("foundation error");
    if (failure.has_value() || (failure.error() != "foundation error")) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
