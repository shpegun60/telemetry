#!/usr/bin/env python3
"""Shared counted-execution and linked-symbol gates for qualification probes.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Controls deliberately remove checks and retain representative newlib symbols;
they run before each qualification build, independently of C++ check totals.
"""
import hashlib
import json
from pathlib import Path
import re

EXPECTED_CONSUMER_CHECKS = 97

ALLOCATORS = frozenset((
    'malloc', 'calloc', 'realloc', 'reallocf', 'free', 'memalign',
    'aligned_alloc', 'posix_memalign', 'valloc', 'pvalloc', 'sbrk',
))
FORMATTERS = frozenset((
    'printf', 'vprintf', 'fprintf', 'vfprintf', 'sprintf', 'vsprintf',
    'snprintf', 'vsnprintf', 'dprintf', 'vdprintf', 'asprintf', 'vasprintf',
    'iprintf', 'viprintf', 'fiprintf', 'vfiprintf', 'siprintf', 'vsiprintf',
    'sniprintf', 'vsniprintf', 'svfprintf', 'svfiprintf', 'sfvwrite', 'sprint',
))


def counted_checks(text, label, expected):
    """A positive count alone cannot prove that all intended checks ran."""
    counted = json.loads(text)
    if (type(counted.get('checks')) is not int or
            type(counted.get('failures')) is not int or
            counted['checks'] != expected or counted['failures'] != 0):
        raise RuntimeError(f'{label}: expected {expected} checks and zero failures, got {counted}')
    return counted['checks']


def forbidden_symbols(nm_output, *, legacy=False):
    """Inspect full demangled names from nm -C, including newlib _name_r.

    Strip only C spelling/clone suffixes when classifying C functions. C++
    names remain intact, so an unrelated method named free() is not refused.
    The scaling fixture may retain legacy Scalar; the mixed consumer may not.
    """
    names = []
    for line in nm_output.splitlines():
        match = re.match(r'^\s*(?:[0-9a-fA-F]+\s+)?[a-zA-Z?]\s+(.+?)\s*$', line)
        if match:
            names.append(match.group(1))
    if not names:
        raise RuntimeError('No symbols parsed from nm output')
    rejected = []
    for name in names:
        base = name.split('@', 1)[0].split('.', 1)[0].lstrip('_')
        if base.endswith('_r'):
            base = base[:-2]
        allocation = base in ALLOCATORS or bool(re.search(r'\boperator (?:new|delete)(?:\[\])?\(', name))
        formatting = base in FORMATTERS or bool(re.search(r'\bto_chars\(', name))
        if allocation or formatting or (legacy and 'Scalar::' in name):
            rejected.append(name)
    return rejected


def linked_control(flags, out, run, nm):
    """Retain real newlib functions, run real nm, and require rejection.

    The negative image has its own libc/libnosys link group; normal probe
    images keep their original link flags. Neither ARM image is executed.
    `run(command, label)` returns stdout and saves the command/result log.
    """
    source = Path(__file__).with_name('SymbolControl.cpp')
    obj, image = out / 'symbol-control.o', out / 'symbol-control.elf'
    run(flags + ['-O2', '-c', source, '-o', obj], 'symbol-control-build')
    run(flags + ['-O2', obj, '-nostdlib', '-Wl,-e,main', '-Wl,--gc-sections',
                 '-Wl,--start-group', '-lc', '-lgcc', '-lnosys', '-Wl,--end-group',
                 '-o', image], 'symbol-control-link')
    symbols = run([nm, '-C', image], 'symbol-control-nm')
    for name in ('_malloc_r', '_printf_r'):
        if not re.search(r'^[0-9a-fA-F]+\s+T\s+' + name + r'$', symbols, re.M):
            raise RuntimeError('Negative control did not retain a defined ' + name)
    rejected = forbidden_symbols(symbols)
    if not {'_malloc_r', '_printf_r'}.issubset(rejected):
        raise RuntimeError('Real newlib negative image was accepted')
    return dict(execution='nm only', required=['_malloc_r', '_printf_r'], rejected_symbols=rejected,
                elf_sha256=hashlib.sha256(image.read_bytes()).hexdigest())


def controls():
    """Show that fewer checks and newlib aliases cannot pass these gates."""
    counted_checks('{"checks":97,"failures":0}', 'consumer-positive', EXPECTED_CONSUMER_CHECKS)
    counted_checks('{"checks":130,"failures":0}', 'scaling-positive', 32 * 4 + 2)
    count_mutations = (
        '{"checks":57,"failures":0}', '{"checks":98,"failures":0}',
        '{"checks":0,"failures":0}', '{"checks":97,"failures":1}',
        '{"checks":true,"failures":0}', '{"checks":97}',
    )
    for text in count_mutations:
        try:
            counted_checks(text, 'count-mutation', EXPECTED_CONSUMER_CHECKS)
        except RuntimeError:
            continue
        raise AssertionError('Changed check count accepted')

    safe = '00000000 T readAs\n         U memcpy\n00000004 T resource::freeSlot()\n'
    if forbidden_symbols(safe):
        raise AssertionError('Ordinary probe symbols rejected')
    mutations = sorted(ALLOCATORS | FORMATTERS)
    mutations += ['_' + name + '_r' for name in sorted(ALLOCATORS | FORMATTERS)]
    mutations += [
        '_malloc_r.constprop.0', 'malloc@@LIBC',
        'operator new(unsigned int)', 'operator new[](unsigned int)',
        'operator delete(void*)', 'operator delete[](void*)',
        'std::to_chars(char*, char*, double)', 'telemetry::Scalar::convert()',
    ]
    for name in mutations:
        if forbidden_symbols('00000000 T ' + name + '\n', legacy=True) != [name]:
            raise AssertionError('Retained symbol accepted: ' + name)
    scalar = '00000000 T telemetry::Scalar::convert()\n'
    if forbidden_symbols(scalar):
        raise AssertionError('Frozen legacy comparison cannot be inspected separately')
    try:
        forbidden_symbols('not nm output')
    except RuntimeError:
        pass
    else:
        raise AssertionError('Empty symbol parse accepted')
    return dict(count_positives=2, count_rejections=len(count_mutations),
                symbol_positives=2, symbol_rejections=len(mutations) + 1)


if __name__ == '__main__':
    print(json.dumps(controls(), sort_keys=True))
