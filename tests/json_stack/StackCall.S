/* Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
 * Privileged Thread mode only, entered on MSP with maskable IRQs disabled.
 * r0 = callback, r1 = 8-byte-aligned top of an independent descending stack.
 * The callback follows AAPCS; r4/r5 retain the original CONTROL and PSP.
 * The caller's saved registers and return address remain on the original MSP.
 */
.syntax unified
.thumb
.section .text.telemetry_call_on_psp,"ax",%progbits
.global telemetry_call_on_psp
.type telemetry_call_on_psp,%function
.thumb_func
telemetry_call_on_psp:
    push {r4-r6, lr}
    mrs r4, control
    mrs r5, psp
    msr psp, r1
    orr r2, r4, #2
    msr control, r2
    isb
    blx r0
    msr control, r4
    isb
    msr psp, r5
    pop {r4-r6, pc}
.size telemetry_call_on_psp, .-telemetry_call_on_psp
.section .note.GNU-stack,"",%progbits
