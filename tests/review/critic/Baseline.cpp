// Critic trial: compile-time baseline, a translation unit with only <cstdio>.
#include <cstdio>

int criticBaseline(const char* text) { return std::puts(text); }
