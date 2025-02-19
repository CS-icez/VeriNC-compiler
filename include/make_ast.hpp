#pragma once
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

template <typename> struct is_tuple : std::false_type {};
template <typename... Ts> struct is_tuple<std::tuple<Ts...>> : std::true_type {};
template <typename T> constexpr bool is_tuple_v = is_tuple<T>::value;

template <typename T>
auto wrap_to_tuple(T&& arg) {
    if constexpr (is_tuple_v<std::decay_t<T>>) {
        return std::forward<T>(arg);
    } else {
        return std::make_tuple(std::forward<T>(arg));
    }
}

template <typename... Args>
auto merge_tuples(Args&&... args) {
    return std::tuple_cat(wrap_to_tuple(std::forward<Args>(args))...);
}

template <typename T, typename... Args>
T* make_ast(Args&&... args) {
    auto combined = merge_tuples(std::forward<Args>(args)...);
    auto f = [](auto&&... params) {
        return new T(std::forward<decltype(params)>(params)...);
    };
    return std::apply(f, combined);
}

template <typename T>
std::vector<T*>* make_vec(T* arg) {
    return new std::vector<T*>{arg};
}

constexpr auto n1 = std::make_tuple(nullptr);
constexpr auto n2 = std::tuple_cat(n1, n1);
constexpr auto n3 = std::tuple_cat(n2, n1);
constexpr auto n4 = std::tuple_cat(n3, n1);
constexpr auto n5 = std::tuple_cat(n4, n1);
constexpr auto n6 = std::tuple_cat(n5, n1);
constexpr auto n7 = std::tuple_cat(n6, n1);
constexpr auto n8 = std::tuple_cat(n7, n1);
constexpr auto n9 = std::tuple_cat(n8, n1);
