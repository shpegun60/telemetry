#!/usr/bin/env python3
"""Compare complete field/value/command JSON and embedded CRCs against ABI 5."""
# Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--baseline-ref', default='b3f0fa6818293fd8e995ce0c53115d0a4a0ce0bc')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    output = args.build_dir.resolve(); output.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(['git', 'rev-parse', args.baseline_ref], cwd=ROOT, text=True).strip()
    files = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', revision, 'lib'],
                                    cwd=ROOT, text=True).splitlines()
    baseline = output / 'baseline'
    for name in files:
        path = baseline / name
        if not path.resolve().is_relative_to(baseline.resolve()):
            raise RuntimeError('Snapshot path escaped its build directory')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(subprocess.check_output(['git', 'show', revision + ':' + name], cwd=ROOT))
    docs = []
    env = os.environ.copy()
    env['PATH'] = str(Path(args.cxx).resolve().parent) + os.pathsep + env['PATH']
    for label, source, old in (('baseline', baseline, 1), ('current', ROOT, 0)):
        executable = output / (label + ('.exe' if os.name == 'nt' else '.out'))
        lib = source / 'lib/telemetry'
        command = [args.cxx, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
            '-pedantic-errors', '-I' + str(lib), f'-DTELEMETRY_BASELINE_IDS={old}',
            str(ROOT / 'tests/position_tables/SchemaParity.cpp'), str(lib / 'abi/TelemetryAbi.cpp'),
            str(lib / 'serialization/TelemetryJson.cpp'), str(lib / 'serialization/TelemetryCommandJson.cpp'),
            '-o', str(executable)]
        built = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        (output / (label + '-build.log')).write_bytes(built.stdout)
        if built.returncode: raise RuntimeError(f'{label} failed: {built.stdout.decode(errors="replace")}')
        doc = subprocess.check_output([str(executable)], env=env).replace(b'\r\n', b'\n')
        for line in doc.splitlines(): json.loads(line)
        docs.append(doc); (output / (label + '.jsonl')).write_bytes(doc)
    if len(docs[0].splitlines()) != 3 or docs[0] != docs[1]:
        raise RuntimeError('Field/value/command JSON or schema fingerprint differs')
    receipt = dict(baseline=revision, documents=3, identical=True, jsonl_sha256=hashlib.sha256(docs[0]).hexdigest())
    (output / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
