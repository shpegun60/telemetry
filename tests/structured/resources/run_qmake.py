#!/usr/bin/env python3
"""Build independent resource, RPC client/server/session qmake selections. MIT."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def input_hashes():
    paths = [path for directory in (ROOT / 'lib', HERE)
             for path in directory.rglob('*')
             if path.is_file() and '__pycache__' not in path.parts]
    paths.append(ROOT / '.github/workflows/ci.yml')
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(paths)}


def cpp_names(text):
    return re.findall(r'([A-Za-z0-9_]+\.cpp)(?=[\s"]|$)', text.replace('\\', '/'))


def variables(makefiles, name):
    # qmake splits SOURCES/OBJECTS across continuation lines. Dependency rules
    # may name inactive conditional headers, so never use whole-file presence.
    for makefile in makefiles:
        joined = re.sub(r'\\\r?\n\s*', ' ', makefile.read_text(errors='replace'))
        for value in re.findall(r'^' + name + r'\s*=\s*(.*)$', joined, re.M):
            yield value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qmake', required=True)
    parser.add_argument('--make', default='make')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    provenance = dict(
        source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        source_dirty=subprocess.check_output(
            ['git', 'status', '--porcelain=v1', '--untracked-files=all'], cwd=ROOT, text=True).splitlines(),
        input_sha256=input_hashes(), success=False,
        tools={name: dict(path=str(Path(shutil.which(command) or command).absolute()),
                         sha256=digest(Path(shutil.which(command) or command)))
               for name, command in (('qmake', args.qmake), ('make', args.make))})
    receipt = output / 'qmake-inputs.json'
    receipt.write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    summary = {}
    for mode in ('core', 'v3', 'rpc', 'both', 'client', 'client_server', 'sessions', 'all'):
        out = args.build_dir.resolve() / mode; out.mkdir(parents=True, exist_ok=True)
        # A fresh object directory proves absence without deleting historical
        # outputs or mistaking a stale object from an older manifest for input.
        objects = out / ('objects-' + str(time.time_ns()))
        commands = [[args.qmake, str(HERE / 'resources.pro'), 'MODE=' + mode,
                     'OBJECTS_DIR=' + objects.as_posix(),
                     'QMAKE_CXXFLAGS+=-Werror', 'QMAKE_CXXFLAGS+=-UNDEBUG'], [args.make, '-j2']]
        for index, cmd in enumerate(commands):
            result = subprocess.run(cmd, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=300)
            (out / f'{index}.log').write_text(result.stdout, encoding='utf-8')
            if result.returncode: raise RuntimeError(f'{mode}: {cmd}\n{result.stdout}')
        candidates = [out / 'structured_resources', out / 'release/structured_resources.exe', out / 'structured_resources.exe']
        program = next(p for p in candidates if p.is_file())
        execution = subprocess.run([str(program)], cwd=out, check=True, timeout=30,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / 'execute.log').write_text(execution.stdout, encoding='utf-8')
        expected = {'QmakeCheck.cpp', 'DeviceResources.cpp', 'Protocol.cpp'}
        if mode not in ('core', 'client'): expected |= {'StructuredAbi.cpp', 'Adapter.cpp'}
        if mode in ('v3', 'both', 'all'): expected.add('Values.cpp')
        if mode in ('rpc', 'both', 'client_server', 'sessions', 'all'): expected.add('RpcMailbox.cpp')
        if mode in ('client', 'client_server', 'all'): expected.add('RpcClient.cpp')

        # These counts also prove repeated .pri inclusion did not duplicate
        # any source. There is no link-time section-GC assumption in this gate.
        selected = (out / 'selected-sources.txt').read_text().splitlines()
        actual = Counter(cpp_names('\n'.join(selected)))
        if actual != Counter({name: 1 for name in expected}):
            raise RuntimeError(f'{mode}: unexpected qmake SOURCES: {actual}')
        makefile_paths = list(out.glob('Makefile*'))
        source_variables = list(variables(makefile_paths, 'SOURCES'))
        object_variables = list(variables(makefile_paths, 'OBJECTS'))
        if not source_variables or not object_variables:
            raise RuntimeError(f'{mode}: generated source/object manifest is empty')
        for value in source_variables:
            if Counter(cpp_names(value)) != actual:
                raise RuntimeError(f'{mode}: generated SOURCES differs: {value}')
        expected_objects = {Path(name).stem + '.o' for name in expected}
        for value in object_variables:
            names = re.findall(r'([A-Za-z0-9_]+\.(?:o|obj))(?=[\s"]|$)', value)
            if Counter(names) != Counter({name: 1 for name in expected_objects}):
                raise RuntimeError(f'{mode}: generated OBJECTS differs: {value}')
        compiled = '\n'.join(line for line in (out / '1.log').read_text().splitlines()
                             if re.search(r'(?:^|\s)-c(?:\s|$)', line))
        if Counter(cpp_names(compiled)) != actual:
            raise RuntimeError(f'{mode}: actual compile recipes differ: {compiled}')
        built = sorted(p.name for p in objects.rglob('*') if p.suffix in ('.o', '.obj'))
        if Counter(built) != Counter({name: 1 for name in expected_objects}):
            raise RuntimeError(f'{mode}: actual object files differ: {built}')
        makefiles = '\n'.join(p.read_text(errors='replace') for p in makefile_paths)
        assert 'structured_protocol' not in makefiles
        assert 'Bind.cpp' not in makefiles and 'Exchange.cpp' not in makefiles
        if mode == 'core':
            # qmake's dependency scanner also lists includes in inactive #ifdef
            # branches. Check actual compiler paths and linked objects instead.
            paths = '\n'.join(re.findall(r'^INCPATH\s*=.*$', makefiles, re.M))
            assert 'boost_pfr' not in paths
        assert 'telemetry/serialization/TelemetryJson.cpp' not in makefiles.replace('\\', '/')
        selected_headers = (out / 'selected-headers.txt').read_text().replace('\\', '/')
        pool_header_selected = '/RpcSessionPool.hpp' in selected_headers
        if pool_header_selected != (mode in ('sessions', 'all')):
            raise RuntimeError(f'{mode}: session pool header was not explicitly selected')
        summary[mode] = dict(sources=selected, objects=built, object_dir=str(objects),
                             compile_recipes=compiled.splitlines(),
                             session_pool_header=pool_header_selected,
                             execution='host runtime')
        print(mode + ': build/run/source/object checks passed', flush=True)
    (args.build_dir.resolve() / 'qmake-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    provenance['input_sha256_after'] = input_hashes()
    provenance['inputs_unchanged'] = provenance['input_sha256'] == provenance['input_sha256_after']
    provenance['success'] = provenance['inputs_unchanged']
    receipt.write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    if not provenance['inputs_unchanged']:
        raise RuntimeError('Qmake qualification inputs changed during the run')


if __name__ == '__main__':
    main()
