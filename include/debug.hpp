#pragma once
#include <iostream>
#include <format>
#include <string>
#include <vector>

namespace std {
    template <typename T>
    struct formatter<vector<T>> {
        constexpr auto parse(format_parse_context& ctx) {
            return ctx.begin();
        }
        
        auto format(const vector<T>& vec, format_context& ctx) const {
            auto out = ctx.out();
            out = format_to(out, "[");
            
            bool first = true;
            for (const auto& elem : vec) {
                if (!first) {
                    out = format_to(out, ", ");
                }
                out = format_to(out, "{}", elem);
                first = false;
            }
            return format_to(out, "]");
        }
    };

    template <typename T>
    struct formatter<vector<T*>> {
        constexpr auto parse(format_parse_context& ctx) {
            return ctx.begin();
        }
        
        auto format(const vector<T*>& vec, format_context& ctx) const {
            auto out = ctx.out();
            out = format_to(out, "[");
            
            bool first = true;
            for (const auto& elem : vec) {
                if (!first) {
                    out = format_to(out, ", ");
                }
                out = format_to(out, "{}", *elem);
                first = false;
            }
            return format_to(out, "]");
        }
    };
}
    
#if 1
#define DEBUG(fmt, ...) \
    std::cout << std::format(fmt, ##__VA_ARGS__) << std::endl
#define COLOR_VAR "\033[36m"
#define COLOR_RESET "\033[0m"

#define DEBUG_EXP(var) \
    std::cout \
        << COLOR_VAR \
        << std::format("[{}:{}] {} = {}", __FILE__, __LINE__, #var, (var)) \
        << COLOR_RESET << std::endl;
#else
#define DEBUG(fmt, ...)
#define DEBUG_EXP(var)
#endif
