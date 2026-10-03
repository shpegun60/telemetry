"""Maintained ID, slot, numeric and compiler-mode contracts (MIT)."""
from pathlib import Path
import json
import os
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
POSITIVES = ('SlotLifecycleCheck', 'SlotEdgesCheck', 'SlotCallableCheck',
             'SlotOverloadCheck', 'NumericEdges', 'NumericOracle', 'HonorFlagsCheck',
             'IdBoundaryCheck', 'WeakTargetCheck', 'BorrowedBraceCompileFail')
SLOT_CASES = {
    'TelemetryOwnerSlotCompileFail': ('TELEMETRY_OWNER_SLOT_FAIL_CASE',
        {case: 'OwnerSlot requires a non-volatile object type' if case in (13, 14) else 'deleted'
         for case in (1, 2, 3, *range(10, 19))}),
    'TelemetryFunctionSlotCompileFail': ('TELEMETRY_FUNCTION_SLOT_FAIL_CASE',
        {case: 'FunctionSlot requires' if case <= 2 else 'convert|conversion' if case <= 5
         else 'no match|does not provide a call' if case == 21 else 'deleted'
         for case in (*range(1, 6), *range(11, 15), 21)}),
    'TelemetryLateBoundCompileFail': ('TELEMETRY_LATE_BOUND_FAIL_CASE', {
        **{case: 'requires an R' for case in (1, 2, 3)},
        **{case: 'deleted' for case in (4, 5, 6, 7, 23, 24, 25, 26, 33, 34)},
        **{case: 'noexcept' for case in (8, 9, 10, 14, 15, 16)},
        **{case: 'convert|conversion' for case in (11, 30)},
        31: 'convert|conversion|cannot initialize a parameter',
        12: 'exceeds its inline', 13: 'exceeds its inline',
        27: 'compatible reference', 28: 'compatible reference', 29: 'direct owner object', 32: 'InlineBytes too small',
        **{case: 'target signature must match exactly' for case in range(35, 43)}}),
}
ID_CASES = {case: r'error:[^\n]*(?:deleted|no matching|constraints not satisfied)' for case in range(1, 105)}
ID_CASES.update({case: 'invalidIdComponent' for case in (65, 66, 67, 68, 73, 74)})
ID_CASES.update({case: '16-bit ID range' for case in (69, 70, 71, 72)})
ID_CASES.update({case: 'Packed ID must be an integer' for case in range(85, 99)})
ID_CASES.update({case: 'Packed ID is outside the 32-bit range' for case in range(99, 105)})
REVIEW_CASES = {**{case: 'invalidIdComponent' for case in (12, 13, 14)},
    15: 'deleted', **{case: 'signature must match exactly' for case in (19, 20, 25)},
    **{case: 'deleted' for case in (21, 22, 24)}, 23: 'actual object|direct owner'}
NULL_VALID = (1, 2, 3, 4, 5, 6, 8, 10, 11, 12, 13)
NULL_INVALID = (20, 23, 25, 26)
WEAK_VALID = (0, 1, 2, 3, 4, 5, 6, 11)

# Counts come from the fixture branches and loop bounds, independently of their
# output. The oracle is 121*1028 + 2*11*6 + 126 +
# 11*(256+256+65536+65536+2): pairs, special values, endpoints, small integers.
RUNTIME_CHECKS = {
    'SlotLifecycleCheck-run': 56, 'SlotEdgesCheck-run': 13,
    'SlotCallableCheck-run': 26, 'SlotOverloadCheck-run': 8,
    'NumericEdges-run': 78, 'NumericOracle-run': 1572092,
    'HonorFlagsCheck-run': 6, 'IdBoundaryCheck-run': 71,
    'BorrowedBraceCompileFail-run': 15,
    **{'id-name-' + str(case) + '-run': 1 for case in range(1, 5)},
    'debug-Og-run': 2, 'WeakTargetCheck-run': 5, 'weak-override-run': 3,
}
PE_WEAK_SKIP = 'SKIP ELF weak target execution'

def runtime_check_count(label, output, platform=None):
    platform = platform or ('pe' if os.name == 'nt' else 'elf')
    if platform not in ('pe', 'elf') or label not in RUNTIME_CHECKS:
        raise RuntimeError(label + ': unregistered runtime counter contract')
    if platform == 'pe' and label == 'weak-override-run':
        raise RuntimeError(label + ': ELF-only runtime fixture')
    lines = output.splitlines()
    counters = [line for line in lines if line.startswith('CHECKS')]
    if len(counters) != 1 or re.fullmatch(r'CHECKS (0|[1-9][0-9]*)', counters[0]) is None:
        raise RuntimeError(label + ': missing, malformed or duplicate runtime counter')
    expected = RUNTIME_CHECKS[label]
    skips = [line for line in lines if line.startswith('SKIP')]
    if platform == 'pe' and label == 'WeakTargetCheck-run':
        expected = 0
        if skips != [PE_WEAK_SKIP]:
            raise RuntimeError(label + ': missing or ambiguous PE weak-target skip marker')
    elif skips:
        raise RuntimeError(label + ': unexpected skipped runtime coverage')
    actual = int(counters[0].split()[1])
    if actual != expected:
        raise RuntimeError(label + ': expected ' + str(expected) + ' runtime checks, got ' + str(actual))
    return actual

def runtime_count_gate_controls():
    accepted, rejected = 0, 0
    kinds = ('zero', 'missing', 'reduced', 'increased', 'duplicate')
    for platform in ('pe', 'elf'):
        for label, count in RUNTIME_CHECKS.items():
            if platform == 'pe' and label == 'weak-override-run':
                continue
            prefix = PE_WEAK_SKIP + '\n' if platform == 'pe' and label == 'WeakTargetCheck-run' else ''
            count = 0 if prefix else count
            good = prefix + 'CHECKS ' + str(count) + '\n'
            if runtime_check_count(label, good, platform) != count:
                raise RuntimeError('Runtime counter control rejected its known valid output')
            accepted += 1
            mutations = ('CHECKS 0\n', prefix, prefix + 'CHECKS ' + str(count - 1) + '\n',
                prefix + 'CHECKS ' + str(count + 1) + '\n', good + 'CHECKS ' + str(count) + '\n')
            for kind, changed in zip(kinds, mutations):
                try:
                    runtime_check_count(label, changed, platform)
                except RuntimeError:
                    rejected += 1
                else:
                    raise RuntimeError(label + ': runtime gate accepted ' + kind + ' control')
        try:
            runtime_check_count('unregistered-run', 'CHECKS 1\n', platform)
        except RuntimeError:
            rejected += 1
        else:
            raise RuntimeError('Runtime gate accepted an unregistered label')
    return dict(valid_outputs=accepted, rejected_mutations=rejected, platforms=['pe', 'elf'],
        mutation_kinds=list(kinds), expectation_source='fixed fixture branches and loop bounds',
        zero_exception='PE WeakTargetCheck requires its exact skip marker; no runtime conditions claimed')

class Commands:
    def __init__(self, root, output, environment=None):
        self.root, self.output = Path(root).resolve(), Path(output).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.environment = environment or os.environ.copy()
        self.counts = dict(commands=0, passed=0, rejected=0, runtime_executions=0, runtime_checks=0, intentional_aborts=0)
        self.results = []
        self.runtime_gate_controls = None

    def run(self, command, label, rejection=None, execute=False, abort=False):
        command = [str(part) for part in command]
        result = subprocess.run(command, cwd=self.root, env=self.environment, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=180)
        (self.output / (label + '.log')).write_text('COMMAND ' + repr(command) + '\nEXIT ' + str(result.returncode) + '\n' + result.stdout, encoding='utf-8')
        self.counts['commands'] += 1
        diagnostic = rejection
        if rejection in ('deleted', 'deleted|no matching', 'no matching|deleted'):
            diagnostic = r'error:[^\n]*(?:deleted|no matching)'
        if abort:
            valid = result.returncode in ((3, -1073740791, 3221226505) if os.name == 'nt' else (-6,))
        elif rejection:
            valid = result.returncode != 0 and re.search(diagnostic, result.stdout, re.I | re.S)
            if '-fsyntax-only' in command:
                valid = valid and re.search(r'\berror:', result.stdout, re.I)
        else:
            valid = result.returncode == 0
        runtime_checks, runtime_error = None, None
        if valid and execute and not abort:
            try:
                runtime_checks = runtime_check_count(label, result.stdout)
            except RuntimeError as error:
                runtime_error, valid = str(error), False
        entry = dict(label=label, command=command, exit_code=result.returncode, passed=bool(valid))
        if runtime_checks is not None:
            entry['runtime_checks'] = runtime_checks
        if runtime_error is not None:
            entry['runtime_gate_error'] = runtime_error
        self.results.append(entry)
        self.write_summary()
        if not valid:
            raise RuntimeError((runtime_error or label + ' failed') + '; see ' + str(self.output / (label + '.log')))
        self.counts['rejected' if rejection else 'passed'] += 1
        if execute:
            self.counts['runtime_executions'] += 1
            if abort:
                self.counts['intentional_aborts'] += 1
            else:
                self.counts['runtime_checks'] += runtime_checks
        self.write_summary()
        return result.stdout

    def write_summary(self, **extra):
        if self.runtime_gate_controls is not None:
            extra['runtime_gate_controls'] = self.runtime_gate_controls
        (self.output / 'summary.json').write_text(json.dumps(dict(counts=self.counts, results=self.results, **extra), indent=2) + '\n', encoding='utf-8')

def syntax_contracts(flags, run, test_root):
    test_root = Path(test_root)
    for name, (macro, cases) in SLOT_CASES.items():
        for case, diagnostic in cases.items():
            run([*flags, '-D' + macro + '=' + str(case), '-fsyntax-only', test_root / (name + '.cpp')], name + '-' + str(case), diagnostic)
    for case in (12, 13):
        run([*flags, '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=1', '-DTELEMETRY_LATE_BOUND_FAIL_CASE=' + str(case),
            '-fsyntax-only', test_root / 'TelemetryLateBoundCompileFail.cpp'], 'slot-no-heap-' + str(case), 'exceeds its inline')
    regressions = test_root / 'regression'
    run([*flags, '-DCASE=14', '-fsyntax-only', regressions / 'OwnerLifetimeCompileFail.cpp'], 'owner-lifetime-14', 'deleted')
    for case in (0, *range(43, 57)):
        run([*flags, '-DTELEMETRY_BORROWED_BRACE_FAIL_CASE=' + str(case), '-fsyntax-only', regressions / 'BorrowedBraceCompileFail.cpp'],
            'borrowed-brace-' + str(case), r'error:[^\n]*deleted' if case else None)
    for case in range(16):
        run([*flags, '-DTELEMETRY_SLOT_CALLABLE_FAIL_CASE=' + str(case), '-fsyntax-only', regressions / 'SlotCallableCompileFail.cpp'],
            'slot-callable-' + str(case), 'signature must match exactly' if case else None)
    for case, diagnostic in REVIEW_CASES.items():
        run([*flags, '-DCASE=' + str(case), '-fsyntax-only', regressions / 'ReviewCompileFail.cpp'], 'review-contract-' + str(case), diagnostic)
    for case in (0, *ID_CASES):
        run([*flags, '-DTELEMETRY_ID_BOUNDARY_FAIL_CASE=' + str(case), '-fsyntax-only', regressions / 'IdBoundaryCompileFail.cpp'],
            'id-boundary-' + str(case), ID_CASES.get(case))
    for case in range(1, 10):
        run([*flags, '-DCASE=' + str(case), *(['-DPACKING'] if case >= 7 else []), '-fsyntax-only', regressions / 'ExplicitIdTemplateArgCompileFail.cpp'],
            'explicit-id-' + str(case), r'error:[^\n]*(?:no matching|deleted|constraints not satisfied)')
    null_flags = flags if '-fno-delete-null-pointer-checks' in flags else [*flags, '-fno-delete-null-pointer-checks']
    for case in (*NULL_VALID, *NULL_INVALID):
        run([*null_flags, '-DCASE=' + str(case), '-fsyntax-only', regressions / 'NullChecksMatrix.cpp'], 'null-presence-' + str(case),
            r'error:[^\n]*(?:static assertion failed|static_assert failed)[^\n]*null' if case in NULL_INVALID else None)
    for case in WEAK_VALID:
        run([*flags, '-DTELEMETRY_WEAK_TARGET_FAIL_CASE=' + str(case), '-fsyntax-only', regressions / 'WeakTargetInstantiation.cpp'], 'weak-target-' + str(case))

def header_and_fp_checks(flags, run, root, output, compiler_version):
    root = Path(root)
    library = root / 'lib/telemetry'
    headers = sorted(path for path in library.rglob('*') if path.suffix in ('.h', '.hpp'))
    if not headers:
        raise RuntimeError('Missing final telemetry headers: ' + str(library))
    probe = Path(output) / 'HeaderCheck.cpp'
    probe.write_text('int main() {}\n', encoding='utf-8')
    for index, header in enumerate(headers):
        run([*flags, '-include', header, '-fsyntax-only', probe], 'header-' + str(index))
    header = library / 'detail/NumberConversion.hpp'
    for option in ('-ffast-math', '-ffinite-math-only', '-Ofast', '-D_M_FP_FAST=1'):
        run([*flags, option, '-include', header, '-fsyntax-only', probe], 'fp-' + option.replace('/', '_'), 'Compile telemetry conversions without')
    if 'clang' in compiler_version.lower():
        for option in ('-fno-honor-nans', '-fno-honor-infinities'):
            for optimization in ('-O1', '-O2'):
                run([*flags, optimization, option, '-Wno-error', '-Wno-nan-infinity-disabled', '-include', header, '-fsyntax-only', probe],
                    'fp-' + option + optimization, 'currently enabled floating-point options')
        run([*flags, '-ffp-model=fast', '-include', header, '-fsyntax-only', probe], 'fp-model-fast', 'Compile telemetry conversions without')
    return [str(path.relative_to(root)).replace('\\', '/') for path in headers]
