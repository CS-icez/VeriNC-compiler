#pragma once
#include <ranges>
#include <string>

template <typename R>
concept StringRange = std::ranges::input_range<R>
    && (std::same_as<std::ranges::range_value_t<R>, std::string*>
        || std::same_as<std::ranges::range_value_t<R>, std::string>);

template <StringRange R>
std::string join(R&& range, const std::string& sep) {
    using elem_t = std::ranges::range_value_t<R>;
    std::string res;
    bool is_first = true;
    for (const auto& s : range) {
        if (!is_first) {
            res += sep;
        }
        is_first = false;
        if constexpr (std::same_as<elem_t, std::string*>) {
            res += *s;
        } else {
            res += s;
        }
    }
    return res;
}
