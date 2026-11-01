#pragma once
#include <cstdio>
#define LOG_INF(tag, fmt, ...) std::printf(fmt "\n" __VA_OPT__(, ) __VA_ARGS__)
#define LOG_ERR(tag, fmt, ...) std::printf(fmt "\n" __VA_OPT__(, ) __VA_ARGS__)
