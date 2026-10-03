#!/usr/bin/env python3
"""Stage 15 software contract: API, immutable wire fixtures and dependency pins.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
ARM images are inspected offline. This runner never opens a device.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'qualification'))
from gates import counted_checks, forbidden_symbols  # noqa: E402

EXPECTED_CHECKS = 29  # 12 original + 13 traversal + 4 conversion conditions.
DIAGNOSTICS = (
    'Structured field accepts name and bindings only; semantic metadata is not supported',
    'Command must return telemetry::CommandResult',
    'Command request must be an aggregate struct or void',
    'Service response must be an aggregate struct or void',
    'Structured endpoints must be noexcept',
    'Request must be by value or const lvalue reference, never volatile or a pointer',
    'Structured command accepts name and one binding only; semantic metadata is not supported',
    'Field readAs permits numeric conversion or the exact structural type',
    r'(?:no type named .Scalar.|.Scalar. .*does not name a type)',
    r'no matching function for call to .*service',
)


def digest(payload):
    return hashlib.sha256(payload).hexdigest()


def git_blob(path):
    """Canonical tracked bytes, also checked against the current working copy."""
    blob = subprocess.check_output(['git', 'show', 'HEAD:' + path], cwd=ROOT)
    actual = (ROOT / path).read_bytes()
    if actual.replace(b'\r\n', b'\n') != blob.replace(b'\r\n', b'\n'):
        raise RuntimeError('Dependency differs from its tracked payload: ' + path)
    return blob


def verify_fixtures():
    contract = json.loads((HERE / 'contract.json').read_text(encoding='utf-8'))
    if contract['format_version'] != 1:
        raise RuntimeError('Unsupported contract manifest version')
    wire = contract['wire']
    if wire['byte_order'] != 'little-endian' or wire['fingerprint']['algorithm'] != 'FNV-1a-64':
        raise RuntimeError('Unsupported canonical byte order or fingerprint contract')
    pfr = contract['dependencies']['boost_pfr']
    prefix = 'lib/boost_pfr/include/'
    names = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', 'HEAD', prefix],
                                    cwd=ROOT, text=True).splitlines()
    if len(names) != pfr['headers']:
        raise RuntimeError('PFR header count changed')
    tree = hashlib.sha256()
    for name in sorted(names):
        tree.update(name[len(prefix):].encode('utf-8') + b'\0')
        tree.update(bytes.fromhex(digest(git_blob(name))))
    if tree.hexdigest() != pfr['tree_sha256']:
        raise RuntimeError('PFR header pin changed')
    if digest(git_blob('lib/boost_pfr/LICENSE_1_0.txt')) != pfr['license_sha256']:
        raise RuntimeError('PFR license pin changed')
    version = (ROOT / 'lib/boost_pfr/VERSION.md').read_text(encoding='utf-8')
    if pfr['tag'] not in version or pfr['commit'] not in version:
        raise RuntimeError('PFR provenance document differs from the pin')
    magic = contract['dependencies']['magic_enum']
    if digest(git_blob('lib/magic_enum/magic_enum.hpp')) != magic['header_sha256']:
        raise RuntimeError('magic_enum header pin changed')
    version = (ROOT / 'lib/magic_enum/README.md').read_text(encoding='utf-8')
    if magic['version'] not in version or magic['commit'] not in version:
        raise RuntimeError('magic_enum provenance document differs from the pin')
    for name, expected in contract['goldens'].items():
        payload = bytes.fromhex((ROOT / name).read_text(encoding='utf-8'))
        if len(payload) != expected['bytes'] or digest(payload) != expected['sha256']:
            raise RuntimeError('Frozen wire bytes changed: ' + name)
        magic = wire['values_magic'] if name.endswith('/values.hex') else wire['descriptor_magic']
        if payload[:4] != magic.encode('ascii') or struct.unpack_from('<HH', payload, 4) != (wire['major'], wire['minor']):
            raise RuntimeError('Manifest version/magic differs from frozen bytes: ' + name)
    return contract


def manifest_assertions(contract):
    """Keep the readable manifest tied to actual constants, never sizeof(C++ rows)."""
    wire = contract['wire']
    lines = ['#include <telemetry_structured/Structured.hpp>',
             '#include <resource/structured/BinaryFormat.hpp>']
    for key, name in (('major', 'binaryMajor'), ('minor', 'binaryMinor'),
                      ('descriptor_header_bytes', 'descriptorHeaderBytes'),
                      ('values_header_bytes', 'valuesHeaderBytes'),
                      ('record_header_bytes', 'recordHeaderBytes'), ('record_version', 'recordVersion')):
        lines.append(f'static_assert(resource::structured::{name} == {int(wire[key])});')
    for key, name in (('record_kinds', 'RecordKind'), ('categories', 'Category'),
                      ('capabilities', 'Capability'), ('value_status', 'ValueStatus')):
        for entry, code in wire[key].items():
            lines.append(f'static_assert(static_cast<unsigned>(resource::structured::{name}::{entry}) == {int(code)});')
    for key, name in (('type_kinds', 'TypeKind'), ('scalar_codes', 'ScalarCode')):
        for entry, code in wire[key].items():
            lines.append(f'static_assert(static_cast<unsigned>(telemetry::structured::{name}::{entry}) == {int(code)});')
    for key, name in (('basis', 'fingerprintBasis'), ('prime', 'fingerprintPrime')):
        value = int(wire['fingerprint'][key], 16)
        lines.append(f'static_assert(resource::structured::{name} == {value}ULL);')
    lines += [f'static_assert(telemetry::structured::structuredAbiRevision == {int(contract["structured_abi_revision"])});',
              f'static_assert(TELEMETRY_STRUCTURED_LOCAL_BYTES == {int(contract["default_local_object_bytes"])});']
    for key, name in (('packed_id_bits', 'telemetry::PackedId'),
                      ('group_bits', 'telemetry::GroupId'), ('entry_bits', 'telemetry::EntryOffset'),
                      ('fingerprint_bits', 'std::uint64_t')):
        lines.append(f'static_assert(std::numeric_limits<{name}>::digits == {int(wire[key])});')
    enum = contract['dependencies']['magic_enum']
    major, minor, patch = map(int, enum['version'].split('.'))
    lines += [f'static_assert(MAGIC_ENUM_VERSION_MAJOR == {major});',
              f'static_assert(MAGIC_ENUM_VERSION_MINOR == {minor});',
              f'static_assert(MAGIC_ENUM_VERSION_PATCH == {patch});',
              'enum class FreezeEnum : std::int16_t { Value };',
              f'static_assert(magic_enum::customize::enum_range<FreezeEnum>::min == {int(enum["automatic_range"][0])});',
              f'static_assert(magic_enum::customize::enum_range<FreezeEnum>::max == {int(enum["automatic_range"][1])});']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--null-checks', action='store_true')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.arm and args.sanitize:
        parser.error('ARM qualification is offline and does not link host sanitizers')
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Keep clang++'s driver spelling instead of resolving it to clang.
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    env = os.environ.copy()
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    report = dict(execution='offline compile/link/inspect' if args.arm else 'host execution',
                  checks=0, commands=0, compile_rejections=0, frames={}, sections={})

    def run(command, label, diagnostic=None):
        command = list(map(str, command))
        start = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                                errors='replace', timeout=180)
        elapsed = time.perf_counter() - start
        (out / (label + '.log')).write_text(
            f'COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {elapsed:.6f}\n{result.stdout}',
            encoding='utf-8')
        if diagnostic is not None:
            if result.returncode == 0 or not re.search(diagnostic, result.stdout, re.S):
                raise RuntimeError(label + ': expected factory diagnostic missing\n' + result.stdout)
            report['compile_rejections'] += 1
        elif result.returncode:
            raise RuntimeError(label + ': command failed\n' + result.stdout)
        else:
            report['commands'] += 1
        print(f'{label}: pass ({elapsed:.2f}s)', flush=True)
        return result.stdout

    report['contract'] = verify_fixtures()
    manifest_header = out / 'Manifest.hpp'
    manifest_header.write_text(manifest_assertions(report['contract']), encoding='utf-8')
    inputs = sorted(path for directory in (ROOT / 'lib', HERE)
                    for path in directory.rglob('*')
                    if path.is_file() and path.suffix in ('.h', '.hpp', '.cpp', '.pri', '.py', '.json'))
    inputs += [HERE.parent / 'traversal/Fixture.hpp', HERE.parent / 'qualification/gates.py']
    inputs += [ROOT / name for name in report['contract']['goldens']]

    def input_hashes():
        return {path.relative_to(ROOT).as_posix(): digest(path.read_bytes().replace(b'\r\n', b'\n'))
                for path in inputs}

    before = input_hashes()
    version = run([compiler, '--version'], 'compiler')
    report['compiler'] = version.splitlines()[0]
    report['compiler_sha256'] = digest(compiler.read_bytes())
    report['source_head'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    flags = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors',
             '-fdiagnostics-color=never', '-ffunction-sections', '-fdata-sections',
             '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.null_checks:
        flags += ['-fno-delete-null-pointer-checks']
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                  '-fno-exceptions', '-fno-rtti', '-fstack-usage', '-DFREEZE_ARM']
    report['flags'] = list(map(str, flags))
    suffix = '.exe' if os.name == 'nt' else ''
    link = ['-Wl,--gc-sections']
    if args.arm:
        link += ['-nostdlib', '-Wl,-e,main', '-lc', '-lgcc']
    elif 'clang' in version.lower():
        link += ['-fuse-ld=lld']

    for opt in ('O2', 'Os', 'Og'):
        obj = out / (opt + '.o')
        image = out / (opt + ('.elf' if args.arm else suffix))
        run(flags + ['-' + opt, '-include', manifest_header, '-c', HERE / 'Contract.cpp', '-o', obj], 'compile-' + opt)
        run(flags + ['-' + opt, obj, *link, '-o', image], 'link-' + opt)
        if not args.arm:
            output = run([image], 'execute-' + opt)
            report['checks'] += counted_checks(output, opt, EXPECTED_CHECKS)
            continue
        def tool(name):
            return compiler.with_name('arm-none-eabi-' + name + suffix)
        symbols = run([tool('nm'), '-C', image], 'symbols-' + opt)
        if forbidden_symbols(symbols, legacy=True):
            raise RuntimeError(opt + ': allocation/formatting/Scalar symbols retained')
        sections = run([tool('size'), '-A', image], 'sections-' + opt)
        parsed = {name: int(size) for name, size in re.findall(r'^(\S+)\s+(\d+)\s+\d+\s*$', sections, re.M)}
        if any(name.startswith(('.init_array', '.preinit_array', '.ctors')) and size
               for name, size in parsed.items()):
            raise RuntimeError(opt + ': static metadata needs startup constructors')
        report['sections'][opt] = parsed
        frames = {}
        for line in obj.with_suffix('.su').read_text().splitlines():
            name, size, kind = line.split('\t')
            if kind != 'static' or int(size) > 512:
                raise RuntimeError(opt + ': large/dynamic individual frame: ' + line)
            frames[name] = int(size)
        if not any('main' in name for name in frames):
            raise RuntimeError(opt + ': main frame missing')
        report['frames'][opt] = frames
        run([tool('objdump'), '-drC', image], 'assembly-' + opt)
    for case, diagnostic in enumerate(DIAGNOSTICS, 1):
        syntax = [flag for flag in flags if flag != '-fstack-usage']
        run(syntax + ['-O2', '-fsyntax-only', '-DCASE=' + str(case), HERE / 'Negative.cpp'],
            'negative-' + str(case), diagnostic)
    if input_hashes() != before:
        raise RuntimeError('Qualification inputs changed during the run')
    report['input_hashes'] = before
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Stage 15 software contract passed:',
          {key: report[key] for key in ('commands', 'checks', 'compile_rejections')}, flush=True)


if __name__ == '__main__':
    main()
