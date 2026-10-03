// Maintained shared contract checks (MIT).
#pragma once
#include <cstdio>
#include <cstdlib>
inline unsigned sharedChecks = 0;
#define CHECK(...) do { ++sharedChecks; if (!(__VA_ARGS__)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__); std::abort(); } } while (false)
inline void reportChecks() { std::printf("CHECKS %u\n", sharedChecks); }
