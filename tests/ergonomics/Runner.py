#!/usr/bin/env python3
"""Failed ergonomics/docs/codegen reruns invalidate previous success evidence.

The deliberate non-C++ compiler fails before the parent runner can invoke this
regression, so nested runs do not recurse. All generated files stay external.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
INVALIDATE = "    (output / 'summary.json').write_text('{\"completed\": false}\\n', encoding='utf-8')\n"
DOCS_INVALIDATE = '    (output / "summary.json").write_text(\'{"completed": false}\\n\', encoding="utf-8")\n'
CODEGEN_INVALIDATE = '\n    (build / "summary.json").write_text(json.dumps(report, indent=2) + "\\n")\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    output = args.build_dir.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('Generated files must remain outside the source checkout')
    output.mkdir(parents=True, exist_ok=True)
    # Microsoft Store's sys.executable is an AppExecLink, which cannot be
    # hashed as a regular compiler image. Use the actual interpreter there.
    noncompiler = Path(sys.base_prefix) / 'python.exe' if sys.platform == 'win32' else Path(sys.executable)
    bootstrap = (
        'import pathlib,sys; source=pathlib.Path(sys.argv[1]); '
        'original=sys.argv[2]; sys.argv=[original,*sys.argv[3:]]; '
        "exec(compile(source.read_text(encoding='utf-8'),original,'exec'),"
        "{'__file__':original,'__name__':'__main__'})"
    )
    report = {'completed': False, 'runner_sha256': {}, 'cases': {}}
    receipt = output / 'summary.json'
    receipt.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    for name, runner, invalidation in (
            ('ergonomics', HERE / 'run.py', INVALIDATE),
            ('docs', ROOT / 'tests/docs/run.py', DOCS_INVALIDATE)):
        original = runner.read_bytes()
        report['runner_sha256'][name] = hashlib.sha256(original).hexdigest()
        source = original.decode('utf-8').replace('\r\n', '\n')
        if source.count(invalidation) != 1:
            raise RuntimeError(name + ': expected exactly one startup receipt invalidation')
        mutant = output / (name + '-negative-control.py')
        mutant.write_text(source.replace(invalidation, '', 1), encoding='utf-8')
        for mode, script in (('fixed', runner), ('negative-control', mutant)):
            label = name + '-' + mode
            child = output / label
            child.mkdir(exist_ok=True)
            previous = {'completed': True, 'prior_run_marker': 'previous-success'}
            summary = child / 'summary.json'
            summary.write_text(json.dumps(previous) + '\n', encoding='utf-8')
            command = ([sys.executable, str(script)] if script == runner else
                       [sys.executable, '-c', bootstrap, str(script), str(runner)])
            compiler = noncompiler if name == 'ergonomics' else child / 'missing-compiler'
            command += ['--cxx', str(compiler), '--build-dir', str(child)]
            result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                                    errors='replace', timeout=30)
            (output / (label + '.log')).write_text(result.stdout, encoding='utf-8')
            after = json.loads(summary.read_text(encoding='utf-8'))
            # Ergonomics must reach actual compilation; docs must fail during
            # compiler hashing, before the old detailed-report try/finally.
            reached_failure = ((child / 'compiler.log').is_file() and
                               (child / 'header-0.log').is_file()) if name == 'ergonomics' else (
                                   'FileNotFoundError' in result.stdout and 'missing-compiler' in result.stdout)
            expected = {'completed': False} if mode == 'fixed' else previous
            report['cases'][label] = {'command': command, 'exit': result.returncode,
                                     'reached_failure': reached_failure, 'summary_after': after}
            receipt.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            if result.returncode == 0 or not reached_failure or after != expected:
                raise RuntimeError(label + ': failed rerun receipt contract changed')
        if runner.read_bytes() != original:
            raise RuntimeError(name + ': runner source changed during regression')
    runner = ROOT / 'tests/codegen/run.py'
    original = runner.read_bytes()
    report['runner_sha256']['codegen'] = hashlib.sha256(original).hexdigest()
    source = original.decode('utf-8').replace('\r\n', '\n')
    if source.count(CODEGEN_INVALIDATE) != 1 or source.count('def tool_snapshot(args):\n') != 1:
        raise RuntimeError('Expected the codegen startup invalidation and tool snapshot')
    # Inject a deterministic selected-tool metadata error, without changing source
    # files or platform permissions. Separate evidence reproduces the actual
    # Windows AppExecLink read failure which exposed this path.
    source = source.replace('def tool_snapshot(args):\n',
                            "def tool_snapshot(args):\n    raise OSError('early tool metadata regression failure')\n", 1)
    for supplement in (False, True):
        for mode in ('fixed', 'negative-control'):
            label = 'codegen-' + ('supplement-' if supplement else '') + mode
            script = output / (label + '.py')
            script.write_text(source if mode == 'fixed' else
                              source.replace(CODEGEN_INVALIDATE, '\n', 1), encoding='utf-8')
            child = output / label
            child.mkdir(exist_ok=True)
            previous = {'passed': True, 'checks': 123, 'prior_run_marker': 'previous-success',
                        'commands': [], 'executions': [], 'negative_controls': [], 'measurements': {}}
            summary = child / 'summary.json'
            summary.write_text(json.dumps(previous) + '\n', encoding='utf-8')
            command = [sys.executable, '-c', bootstrap, str(script), str(runner),
                       '--build-dir', str(child)]
            if supplement:
                command += ['--large-arm-only', '--arm-cxx', 'missing-arm-compiler']
            result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                                    errors='replace', timeout=30)
            (output / (label + '.log')).write_text(result.stdout, encoding='utf-8')
            after = json.loads(summary.read_text(encoding='utf-8'))
            expected = after.get('passed') is False
            if mode == 'negative-control':
                expected = after == previous
            elif supplement:
                expected = expected and all(after.get(key) == value for key, value in previous.items()
                                            if key != 'passed')
                expected = expected and after['supplements'][-1]['original_checks'] == 123
            else:
                expected = expected and 'prior_run_marker' not in after
            reached_failure = 'OSError: early tool metadata regression failure' in result.stdout
            report['cases'][label] = {'command': command, 'exit': result.returncode,
                                     'reached_failure': reached_failure, 'summary_after': after}
            receipt.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            if result.returncode == 0 or not reached_failure or not expected:
                raise RuntimeError(label + ': failed rerun/supplement receipt contract changed')
    if runner.read_bytes() != original:
        raise RuntimeError('Codegen runner source changed during regression')
    report['completed'] = True
    receipt.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Runner reuse: 4 fixed failed runs invalidated; 4 old-behavior controls retained success')


if __name__ == '__main__':
    main()
