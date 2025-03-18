#pragma once
#include <string>
#include <variant>
#include <vector>
#include "ast.hpp"
#include "debug.hpp"
#include "join.hpp"

class exp_t {
    using tla_t = const std::vector<std::string*>*;
public:
    exp_t(const std::string& _value) : value(_value) { }
    exp_t(tla_t _value) : value(_value) { }

    bool operator==(const std::string& s) const {
        if (std::holds_alternative<std::string>(value)) {
            return std::get<std::string>(value) == s;
        } else {
            const auto& vec = *std::get<tla_t>(value);
            return vec.size() == 1 && *vec[0] == s;
        }
    }

    std::string to_string() const {
        // DEBUG("Enter {}", __func__);
        if (std::holds_alternative<std::string>(value)) {
            // DEBUG("{}: is string", __func__);
            // DEBUG("Exit {}", __func__);
            return std::get<std::string>(value);
        }
        // DEBUG("{}: is TLA", __func__);
        const auto& vec = *std::get<tla_t>(value);
        std::string res;
        auto is_ident = [](const std::string& s) {
            return s.size() > 0 && !std::isdigit(s[0])
                && std::ranges::all_of(s, [](char c) {
                    return std::isalnum(c) || c == '_';
                });
        };

        for (size_t i = 0; i < vec.size(); ++i) {
            // DEBUG_EXP(i);
            const auto& curr = *vec[i];
            // DEBUG_EXP(curr);
            const auto& prev = i > 0 ? *vec[i - 1] : "";
            // DEBUG_EXP(prev);
            const auto& next = i + 1 == vec.size() ? "" : *vec[i + 1];
            // Exception rules:
            //   1. ident.field
            //   2. (exp)
            //   3. [exp]
            //   4. {exp}
            //   5. exp, exp
            //   6. ~exp
            //   7. ident[exp] or ident(exp)
            //   8. <= or >= or ==
            //   9. :>
            //  10. =>
            //  11. [exp][exp]
            bool add_space = (i > 0)
                && prev != "." && curr != "."
                && prev != "(" && curr != ")"
                && prev != "[" && curr != "]"
                && prev != "{" && curr != "}"
                && curr != ","
                && prev != "~"
                && !(is_ident(prev) && (curr == "[" || curr == "(") && next != "]")
                && !((prev == "<" || prev == ">" || prev == "=") && curr == "=")
                && !(prev == ":" && curr == ">")
                && !(prev == "=" && curr == ">")
                && !(prev.ends_with("]") && curr == "[" && next != "]");
            if (add_space) {
                res += " ";
            }
            res += curr;
        }
        // DEBUG_EXP(vec);
        // DEBUG_EXP(res);
        // DEBUG("Exit {}", __func__);
        return res;
    }

    static std::string to_string(const ExpAST& exp) {
        assert(exp.rule == ExpAST::TLA
            && "Internal error: calling exp_t::to_string on non-TLA expression");
        return exp_t(exp.tla).to_string();
    }

    static std::string to_string(const std::vector<ExpAST*>& exps) {
        auto f = [](const ExpAST* exp) { return to_string(*exp); };
        return join(exps | std::views::transform(f), ", ");
    }

private:
    std::variant<std::string, tla_t> value;
};
