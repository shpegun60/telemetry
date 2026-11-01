# Bundled delegate assignment regressions

`DelegateInlineCheck.cpp` and `DelegateHeapCheck.cpp` are maintained executable
checks in the shared `tests/run_checks.py` gate. They share `AssignmentChecks.hpp`
and emit exactly `CHECKS 50` and `CHECKS 112`, respectively.

Both delegate families must prepare owning replacements while the previous target
is alive. Cases cover complete targets and callable subobjects at nonzero offsets,
lvalue/rvalue sources, self-move, function-pointer members and conversion objects.
The heap profile also moves a same-type delegate embedded in the old target.

The default profile forbids allocation through a replacement global `operator new`.
The opt-in heap profile checks allocation failure, source-construction exceptions,
deallocation after an allocating constructor throws, and mutable-lvalue-only and
volatile-rvalue-only source categories. The exception-control flag is volatile so
the compiler cannot elide an allocation by proving that its constructor must throw.

The tests also record the intended extra inline move for prepared assignment.
An owning lvalue assignment performs one source copy and one noexcept transfer;
delegate move assignment performs two noexcept transfers. Invocation and public
object layout do not change. Borrow/bind targets still need externally surviving
storage; replacing an owner with a borrow into its destroyed target violates that
existing lifetime contract.

The shared runner executes these tests in its ordinary and ASan/UBSan profiles.
They can also be compiled directly with C++17 or C++20 and `-I lib`.
Exception-injection controls run only when C++ exceptions are enabled; the
50/112 counts above describe those host runs. The shared ARM gate compiles the
applicable assignment cases at `-O2` and `-Os` with exceptions and RTTI disabled.
It does not execute them on an MCU.
