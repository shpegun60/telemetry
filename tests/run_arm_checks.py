#!/usr/bin/env python3
"""Compile final C++20 shared Cortex-M7 contracts and inspect ID boundaries (MIT)."""
import argparse
from pathlib import Path
import os
import re
import shutil
import sys
from regression.checks import Commands, POSITIVES, syntax_contracts, header_and_fp_checks

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent

def function_body(disassembly, name):
    match = re.search(rf'^[0-9a-fA-F]+ <{re.escape(name)}>:\n(.*?)(?=^[0-9a-fA-F]+ <|^Disassembly of section |\Z)',
        disassembly, re.M | re.S)
    if match is None:
        raise RuntimeError('Missing ARM probe: ' + name)
    return match[1]

def normalized_instructions(disassembly, name):
    result = []
    for line in function_body(disassembly, name).splitlines():
        match = re.match(r'\s*[0-9a-fA-F]+:\s+(?:[0-9a-fA-F]{4}\s+)+([a-z][a-z0-9.]*)\s*(.*)', line)
        if not match:
            continue
        operation = match[1].removesuffix('.w').removesuffix('.n')
        operands = re.split(r'\s*[;@]', match[2], maxsplit=1)[0].strip()
        result.append((operation, operands))
    return result

def check_id_boundaries(disassembly):
    for operation in ('local', 'field', 'command', 'group', 'index'):
        name = 'id_boundary_' + operation + '_u64'
        body = function_body(disassembly, name)
        high_word = re.search(r'\bcbn?z\s+r1,', body)
        if not high_word:
            raise RuntimeError(name + ': missing high-word rejection')
        if re.search(r'\b(?:ldr|ldrd|lsrs|uxth|bl|blx)\b', body[:high_word.start()]):
            raise RuntimeError(name + ': ID used before width check')
    for operation, extraction in (('group', 'lsrs'), ('index', 'uxth')):
        name = 'id_boundary_' + operation + '_u32'
        instructions = [(op, args) for op, args in normalized_instructions(disassembly, name) if op != 'nop']
        if len(instructions) != 2 or instructions[0][0] != extraction or instructions[1] != ('bx', 'lr'):
            raise RuntimeError(name + ': native extraction gained work')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('ARM_CXX', 'arm-none-eabi-g++'))
    parser.add_argument('--objdump')
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--null-checks', action='store_true')
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    if compiler is None:
        raise RuntimeError('ARM compiler not found: ' + args.cxx)
    objdump = args.objdump or str(Path(compiler).with_name('arm-none-eabi-objdump' + ('.exe' if os.name == 'nt' else '')))
    root, output = args.source_root.resolve(), args.build_dir.resolve()
    runner = Commands(root, output)
    flags = [compiler, '-std=c++20', '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
        '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra', '-Werror', '-pedantic-errors', '-fdiagnostics-color=never',
        '-I' + str(root / 'lib'), '-I' + str(root / 'lib/boost_pfr/include'), '-I' + str(root / 'lib/magic_enum')]
    if args.null_checks:
        flags.append('-fno-delete-null-pointer-checks')
    version = runner.run([compiler, '--version'], 'compiler-version')
    target = runner.run([compiler, '-dumpmachine'], 'compiler-target').strip()
    if target != 'arm-none-eabi':
        raise RuntimeError('Unexpected compiler target: ' + target)
    probes = 0
    slot_comparisons = {}
    for optimization in ('O2', 'Os'):
        for suite in (*POSITIVES, 'DebugLevelCheck'):
            runner.run([*flags, '-' + optimization, '-fstack-usage', '-c', HERE / 'regression' / (suite + '.cpp'),
                '-o', output / (suite + '-' + optimization + '.o')], suite + '-' + optimization + '-compile')
        obj = output / ('IdBoundaryCodegen-' + optimization + '.o')
        runner.run([*flags, '-' + optimization, '-c', HERE / 'regression/IdBoundaryCodegen.cpp', '-o', obj], 'id-codegen-' + optimization)
        assembly = runner.run([objdump, '-drC', obj], 'id-assembly-' + optimization)
        check_id_boundaries(assembly)
        changed, count = re.subn(r'(\bcbn?z\s+)r1,', r'\g<1>r0,', assembly, count=1)
        if count != 1:
            raise RuntimeError('Missing ID gate mutation control')
        try:
            check_id_boundaries(changed)
        except RuntimeError:
            pass
        else:
            raise RuntimeError('ID gate accepted changed high-word register')
        probes += 1
        slot_obj = output / ('SlotCodegen-' + optimization + '.o')
        runner.run([*flags, '-' + optimization, '-fno-ipa-icf', '-c', HERE / 'regression/SlotCodegen.cpp', '-o', slot_obj], 'slot-codegen-' + optimization)
        slot_asm = runner.run([objdump, '-drC', slot_obj], 'slot-assembly-' + optimization)
        current = {}
        for kind in ('function', 'context', 'borrowed', 'owned', 'owner'):
            direct = normalized_instructions(slot_asm, 'slot_' + kind + '_direct')
            table = normalized_instructions(slot_asm, 'slot_' + kind + '_table')
            direct_body = function_body(slot_asm, 'slot_' + kind + '_direct')
            table_body = function_body(slot_asm, 'slot_' + kind + '_table')
            def references(body):
                words = re.findall(r'\.word\s+(0x[0-9a-fA-F]+)', body)
                relocations = re.findall(r'R_ARM_(?:ABS32|REL32|THM_CALL|THM_JUMP24)\s+([^\n]+)', body)
                return sorted(words), sorted(relocations)
            if references(direct_body) != references(table_body):
                raise RuntimeError(optimization + ': native ' + kind + ' dispatch changed literal or relocation targets')
            # Relocation addresses and symbol display names do not change the
            # instruction stream. Branch labels are normalized separately.
            def comparable(instructions):
                result = []
                for op, operands in instructions:
                    operands = re.sub(r'\b[0-9a-f]+ <[^>]+>', '<target>', operands)
                    operands = re.sub(r'\[pc, #\d+\]', '[pc, #literal]', operands)
                    result.append((op, operands))
                return result
            if kind == 'owner':
                direct_calls = re.findall(r'R_ARM_THM_(?:CALL|JUMP24)\s+([^\n]+)', function_body(slot_asm, 'slot_owner_direct'))
                table_calls = re.findall(r'R_ARM_THM_(?:CALL|JUMP24)\s+([^\n]+)', function_body(slot_asm, 'slot_owner_table'))
                if len([op for op, args in table if op != 'nop']) > len([op for op, args in direct if op != 'nop']) or direct_calls != table_calls or direct_calls != ['Owner::read() const']:
                    raise RuntimeError(optimization + ': owner table gained work or changed its direct method call')
            elif comparable(direct) != comparable(table):
                raise RuntimeError(optimization + ': native ' + kind + ' table dispatch differs from selected-target call')
            current[kind] = dict(direct_instructions=len([op for op, args in direct if op != 'nop']),
                table_instructions=len([op for op, args in table if op != 'nop']),
                gate='no added instructions and same method relocation' if kind == 'owner' else 'identical normalized instructions')
        slot_comparisons[optimization] = current
    runner.run([*flags, '-Og', '-c', HERE / 'regression/DebugLevelCheck.cpp', '-o', output / 'DebugLevelCheck-Og.o'], 'debug-Og-compile')
    syntax_contracts(flags, runner.run, HERE)
    for case in range(1, 5):
        runner.run([*flags, '-DCASE=' + str(case), '-fsyntax-only', HERE / 'regression/IdNameCollisionCheck.cpp'], 'id-name-' + str(case))
    headers = header_and_fp_checks(flags, runner.run, root, output, version)
    runner.write_summary(headers=headers, compiler_version=version.strip(), source_root=str(root), standard='c++20',
        codegen_probes=probes, mutation_controls=probes, slot_comparisons=slot_comparisons, null_checks=args.null_checks,
        limitation='Offline object and diagnostic checks; no target execution or whole-call-chain stack claim')
    print('Shared Cortex-M7 checks passed:', runner.counts, 'codegen probes:', probes, flush=True)

if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        sys.exit(str(error))
