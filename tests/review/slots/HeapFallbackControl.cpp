// Review control (slots): with TINY_DELEGATE_ENABLE_HEAP_FALLBACK=1 the plain
// tiny::delegate stores a 1-byte, throwing-move callable on the heap. The only
// thing keeping DelegateSlot allocation-free for such a target is the slot's
// own nothrow-move static_assert (late-bound case 15), which the runner does
// not re-check in heap mode (it re-checks only cases 12 and 13).
#include "../../../lib/delegate/tiny_delegate.hpp"
#include <cstdio>

struct Move {
    Move() = default;
    Move(Move&&) noexcept(false) {}
    float operator()() noexcept { return 1.f; }
};

int main()
{
    tiny::delegate<float(), 32> d;
    d = Move{};
    std::printf("plain tiny::delegate, sizeof(target)=%u, throwing move: uses_heap=%d\n",
                static_cast<unsigned>(sizeof(Move)), static_cast<int>(d.uses_heap()));
    return 0;
}
