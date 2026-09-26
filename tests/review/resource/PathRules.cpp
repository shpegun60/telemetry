// Review probe (resource slice): path spelling rules versus the README wording.
#include <resource/File.hpp>
using resource::detail::validPath;
static_assert(!validPath("/a\x1f") && !validPath("/a\b") && !validPath("/a/./b") && !validPath("/"));
static_assert(validPath("/a\x7f"), "DEL (0x7f) is accepted although README says no control bytes");
static_assert(validPath("/.hidden") && validPath("/a\xc2\x85"), "C1 NEL as UTF-8 is accepted");
int main() {}
