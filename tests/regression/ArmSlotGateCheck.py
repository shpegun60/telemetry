#!/usr/bin/env python3
"""Check actual ARM slot logs and exercise the padding comparison gate (MIT)."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from run_arm_checks import check_slot_comparisons, comparable_slot_instructions, function_body, normalized_instructions, slot_instructions

# Captured ARM13.2.1 Os function pair from CI run 37155083161. Only the
# direct route has the unreachable alignment nop that caused that gate failure.
CI_FUNCTION_PAIR = """00000022 <slot_function_direct>:
  22: 4b06 ldr r3, [pc, #24]
  24: 681b ldr r3, [r3, #0]
  26: b510 push {r4, lr}
  28: 4604 mov r4, r0
  2a: b913 cbnz r3, 32 <slot_function_direct+0x10>
  2c: 4620 mov r0, r4
  2e: 7123 strb r3, [r4, #4]
  30: bd10 pop {r4, pc}
  32: 4798 blx r3
  34: 2301 movs r3, #1
  36: 6020 str r0, [r4, #0]
  38: e7f8 b.n 2c <slot_function_direct+0xa>
  3a: bf00 nop
  3c: 00000000 .word 0x00000000
      3c: R_ARM_ABS32 .bss

00000040 <slot_function_table>:
  40: 4b05 ldr r3, [pc, #20]
  42: 681b ldr r3, [r3, #0]
  44: b510 push {r4, lr}
  46: 4604 mov r4, r0
  48: b913 cbnz r3, 50 <slot_function_table+0x10>
  4a: 4620 mov r0, r4
  4c: 7123 strb r3, [r4, #4]
  4e: bd10 pop {r4, pc}
  50: 4798 blx r3
  52: 2301 movs r3, #1
  54: 6020 str r0, [r4, #0]
  56: e7f8 b.n 4a <slot_function_table+0xa>
  58: 00000000 .word 0x00000000
      58: R_ARM_ABS32 .bss
"""

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def replace_body(assembly, name, changed):
    original = function_body(assembly, name)
    if assembly.count(original) != 1:
        raise RuntimeError('Probe body is not unique')
    return assembly.replace(original, changed, 1)

def regression_assembly(assembly):
    for name in ('slot_function_direct', 'slot_function_table'):
        pattern = rf'^[0-9a-fA-F]+ <{name}>:\n.*?(?=^[0-9a-fA-F]+ <|^Disassembly of section |\Z)'
        assembly, count = re.subn(pattern, '', assembly, flags=re.M | re.S)
        if count != 1:
            raise RuntimeError('Missing or duplicate function control probe')
    return assembly + '\nDisassembly of section .text.slot_function_control:\n\n' + CI_FUNCTION_PAIR

def gate_controls(assembly):
    original = regression_assembly(assembly)
    check_slot_comparisons(original, 'Os')
    name = 'slot_function_direct'
    body = function_body(original, name)
    before = normalized_instructions(original, name)
    after = slot_instructions(original, name)
    if before[-2:] != [('b', '2c <slot_function_direct+0xa>'), ('nop', '')] or after != before[:-1]:
        raise RuntimeError('Captured CI padding or terminal transfer control changed')
    positives = []
    # Re-address the whole direct function, including its local targets and
    # literal/relocation sites. The branch instruction ordinals stay the same.
    shifted = re.sub(r'^(\s*)([0-9a-f]+):',
        lambda m: m[1] + format(int(m[2], 16) + 0x100, 'x') + ':', body, flags=re.M)
    shifted = re.sub(r'\b([0-9a-f]+) (<slot_function_direct(?:\+0x[0-9a-f]+)?>)',
        lambda m: format(int(m[1], 16) + 0x100, 'x') + ' ' + m[2], shifted)
    shifted_assembly = replace_body(original, name, shifted).replace(
        '00000022 <slot_function_direct>:', '00000122 <slot_function_direct>:', 1)
    check_slot_comparisons(shifted_assembly, 'Os')
    positives.append(dict(control='equivalent-function-start-offset', passed=True))
    table = function_body(original, 'slot_function_table')
    padded = table.replace('4b05 ldr r3, [pc, #20]', '4b06 ldr r3, [pc, #24]', 1)
    padded, count = re.subn(r'^(\s*)58:', r'\g<1>5c:', padded, flags=re.M)
    if count != 2:
        raise RuntimeError('Captured CI literal/relocation padding sites changed')
    padded = padded.replace('  5c: 00000000', '  58: bf00 nop\n  5a: bf00 nop\n  5c: 00000000', 1)
    check_slot_comparisons(replace_body(original, 'slot_function_table', padded), 'Os')
    positives.append(dict(control='equivalent-trailing-padding', passed=True))
    relocated = '00000000 <probe>:\n0: f7ff fffe bl 0 <probe>\n0: R_ARM_THM_CALL targetFunction\n4: 4770 bx lr\n'
    if comparable_slot_instructions(relocated, 'probe') != [('bl', '<relocation:THM_CALL targetFunction>'), ('bx', 'lr')]:
        raise RuntimeError('Relocated instruction-zero call was treated as a local branch')
    positives.append(dict(control='relocated-call-at-instruction-zero', passed=True))
    # Insert a reachable nop after the first ldr, consuming the old alignment
    # nop. Code/branch addresses shift by two bytes; the literal stays at 0x3c.
    reachable = []
    for line in body.splitlines(keepends=True):
        match = re.match(r'(\s*)([0-9a-f]+):(.*)', line)
        if match and int(match[2], 16) == 0x3a:
            continue
        if match and 0x24 <= int(match[2], 16) <= 0x38:
            line = match[1] + format(int(match[2], 16) + 2, 'x') + ':' + match[3] + '\n'
        line = re.sub(r'\b([0-9a-f]+) <slot_function_direct\+0x([0-9a-f]+)>',
            lambda m: (format(int(m[1], 16) + 2, 'x') + ' <slot_function_direct+0x' +
                format(int(m[2], 16) + 2, 'x') + '>') if 0x24 <= int(m[1], 16) <= 0x38 else m[0], line)
        reachable.append(line)
        if match and int(match[2], 16) == 0x22:
            reachable.append('  24: bf00 nop\n')
    def changed(pattern, replacement):
        result, count = re.subn(pattern, replacement, body, count=1)
        if count != 1:
            raise RuntimeError('Missing captured CI mutation site')
        return result
    mutations = (
        ('reachable-nop', ''.join(reachable)),
        ('changed-opcode', changed(r'(24:\s*)681b(\s+)ldr', r'\g<1>601b\g<2>str')),
        ('changed-local-branch-target', changed(r'(2a:\s*)b913(\s+cbnz r3, )32 <slot_function_direct\+0x10>',
            r'\g<1>b91b\g<2>34 <slot_function_direct+0x12>')),
        ('branch-targeted-padding-nop', changed(r'(38:\s*)e7f8(\s+b.n\s+)2c <slot_function_direct\+0xa>',
            r'\g<1>e7ff\g<2>3a <slot_function_direct+0x18>')),
        ('changed-literal', changed(r'(\.word\s+)0x00000000', r'\g<1>0x00000001')),
        ('changed-relocation-target', changed(r'(R_ARM_ABS32\s+)\.bss', r'\g<1>.data')))
    negatives = []
    for label, changed_body in mutations:
        mutated = replace_body(original, name, changed_body)
        try:
            check_slot_comparisons(mutated, 'Os')
        except RuntimeError as error:
            negatives.append(dict(control=label, refused=True, reason=str(error)))
        else:
            raise RuntimeError('Slot gate accepted ' + label)
        if label == 'branch-targeted-padding-nop' and slot_instructions(mutated, name)[-1] != ('nop', ''):
            raise RuntimeError('Explicit branch-target padding was stripped')
    helpers = []
    for label, code, expected in (
        ('b-terminal', '0: e7fe b.n 0 <probe>\n2: bf00 nop\n', [('b', '0 <probe>')]),
        ('bx-lr-terminal', '0: 4770 bx lr\n2: bf00 nop\n', [('bx', 'lr')]),
        ('pop-pc-terminal', '0: bd10 pop {r4, pc}\n2: bf00 nop\n', [('pop', '{r4, pc}')]),
        ('no-terminal', '0: 4604 mov r4, r0\n2: bf00 nop\n', [('mov', 'r4, r0'), ('nop', '')]),
        ('reachable-prefix', '0: bf00 nop\n2: 4770 bx lr\n4: bf00 nop\n', [('nop', ''), ('bx', 'lr')]),
        ('incoming-branch', '0: b100 cbz r0, 4 <probe+0x4>\n2: 4770 bx lr\n4: bf00 nop\n',
            [('cbz', 'r0, 4 <probe+0x4>'), ('bx', 'lr'), ('nop', '')]),
        ('conditional-transfer', '0: d0fe beq.n 0 <probe>\n2: bf00 nop\n', [('beq', '0 <probe>'), ('nop', '')]),
        ('bx-other-register', '0: 4718 bx r3\n2: bf00 nop\n', [('bx', 'r3'), ('nop', '')]),
        ('pop-without-pc', '0: bc10 pop {r4}\n2: bf00 nop\n', [('pop', '{r4}'), ('nop', '')])):
        if slot_instructions('00000000 <probe>:\n' + code, 'probe') != expected:
            raise RuntimeError('Padding helper failed ' + label)
        helpers.append(dict(control=label, passed=True))
    return dict(real_ci_positive=True, positive_controls=positives, negative_controls=negatives, helper_controls=helpers)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    started = datetime.now(timezone.utc).isoformat()
    directory = args.build_dir.resolve()
    assemblies = []
    for optimization in ('O2', 'Os'):
        path = directory / ('slot-assembly-' + optimization + '.log')
        assembly = path.read_text(encoding='utf-8')
        comparisons = check_slot_comparisons(assembly, optimization)
        assemblies.append(dict(path=str(path), sha256=sha(path), comparisons=comparisons))
        if optimization == 'Os':
            controls = gate_controls(assembly)
    counts = dict(assemblies=len(assemblies), slot_comparisons=sum(len(x['comparisons']) for x in assemblies),
        real_ci_positive=int(controls['real_ci_positive']), rejected_mutations=len(controls['negative_controls']),
        positive_controls=len(controls['positive_controls']), helper_controls=len(controls['helper_controls']))
    record = dict(started_utc=started, completed_utc=datetime.now(timezone.utc).isoformat(), counts=counts,
        source_sha256={str(Path(__file__).resolve()): sha(Path(__file__).resolve()),
            str(Path(__file__).resolve().parent.parent / 'run_arm_checks.py'):
                sha(Path(__file__).resolve().parent.parent / 'run_arm_checks.py')},
        assemblies=assemblies, controls=controls,
        scope='Parser-only actual assembly replay and captured CI regression controls; no compilation or target execution')
    (directory / 'arm-slot-gate-controls.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('ARM slot gate checks passed:', counts, flush=True)

if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        sys.exit(str(error))
