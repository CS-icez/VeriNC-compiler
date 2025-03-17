#pragma once
#include <format>
#include <iostream>

struct TriBool {
    enum TriBoolValue { True, False, Both } value;
    
    TriBool() : value(False) { }
    explicit TriBool(TriBoolValue _value) : value(_value) { }
    TriBool(bool _value) : value(_value ? True : False) { }
    TriBool& operator=(bool _value) {
        value = _value ? True : False;
        return *this;
    }

    bool operator==(TriBool _value) const {
        return value == _value.value;
    }
    bool operator==(bool _value) const {
        return *this == TriBool(_value);
    }
    operator bool() const = delete;

    bool is_both() const {
        return value == Both;
    }

    TriBool operator&(const TriBool& other) const {
        if (value == True || other.value == True) {
            return TriBool(True);
        } else if (value == False && other.value == False) {
            return TriBool(False);
        } else {
            return TriBool(Both);
        }
    }
    TriBool& operator&=(const TriBool& other) {
        return *this = *this & other;
    }
    TriBool operator|(const TriBool& other) const {
        return value == other.value ? TriBool(value) : TriBool(Both);
    }
    TriBool& operator|=(const TriBool& other) {
        return *this = *this | other;
    }

    friend std::ostream& operator<<(std::ostream& os, const TriBool& obj) {
        switch (obj.value) {
            case True: os << "true"; break;
            case False: os << "false"; break;
            case Both: os << "both"; break;
        }
        return os;
    }
};

namespace std {
    template <>
    struct formatter<TriBool> {
        constexpr auto parse(format_parse_context& ctx) {
            return ctx.begin();
        }
        
        auto format(const TriBool& tb, format_context& ctx) const {
            auto out = ctx.out();
            switch (tb.value) {
                case TriBool::True: out = format_to(out, "true"); break;
                case TriBool::False: out = format_to(out, "false"); break;
                case TriBool::Both: out = format_to(out, "both"); break;
            }
            return out;
        }
    };
}
