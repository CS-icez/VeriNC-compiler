#pragma once
#include <iostream>
#include <format>

#if 0
#define DEBUG(fmt, ...) \
    std::cout << std::format(fmt, ##__VA_ARGS__) << std::endl
#else
#define DEBUG(fmt, ...)
#endif
