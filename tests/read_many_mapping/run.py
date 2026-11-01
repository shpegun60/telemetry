#!/usr/bin/env python3
"""Qualify experimental mapping via installed modules; never program hardware. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def validate_mapping(report):
    """Independent traffic/selection checks, with no host timing threshold."""
    for flag, expected in dict(passed=True, hardware=False, prototype_only=True,
                               production_read_many_implemented=False,
                               device_client_integrated=False).items():
        if report.get(flag) is not expected:
            raise RuntimeError('Incorrect prototype scope: ' + flag)
    if report.get('checks', 0) < 1 or report.get('timing_iterations') != 200:
        raise RuntimeError('Missing executed controls or timing passes')
    expected_cap = dict(record_bytes=24, protocol_tx_bytes=13, protocol_rx_bytes=36,
                        framed_tx_bytes=19, framed_rx_bytes=42)
    if report.get('capability') != expected_cap:
        raise RuntimeError('Resource capability READ accounting differs')
    controls = report.get('controls', {})
    for name in ('capability', 'request_preflight', 'reply_validation',
                 'schema_session', 'mixed_values', 'service_errors'):
        if not isinstance(controls.get(name), int) or controls[name] < 1:
            raise RuntimeError('Missing control suite: ' + name)
    profiles = report.get('profiles', [])
    if len(profiles) != 10:
        raise RuntimeError('Expected five counts and two getter workloads')
    seen = set()
    for profile in profiles:
        count, work = profile.get('count'), profile.get('getter_work')
        if count not in (1, 8, 12, 16, 32) or work not in (0, 128) or (count, work) in seen:
            raise RuntimeError('Wrong/duplicate workload')
        seen.add((count, work))
        indices = [32 * i // count for i in range(count)]
        ids = [(i // 16 << 16) | (i % 16) for i in indices]
        values = [0 if i == 0 else 0xffffffff if i == 7 else
                  0x10203040 ^ (i * 0x01010101) for i in indices]
        if profile.get('requested_ids') != ids or profile.get('expected_values') != values:
            raise RuntimeError('Wrong cross-catalog selection or scalar dataset')
        checksum = 0
        for value in values:
            checksum = (checksum * 33 + value) & 0xffffffffffffffff
        modes = profile.get('modes', [])
        if len(modes) != 2 or {m.get('name') for m in modes} != {'compact', 'fixed_service'}:
            raise RuntimeError('Both mappings must execute')
        for mode in modes:
            tx, rx = ((34 + 4 * count, 34 + 12 * count) if mode['name'] == 'compact'
                      else (162, 546))
            expected = dict(getter_calls=count, exchanges=1, protocol_tx_bytes=tx,
                            protocol_rx_bytes=rx, framed_tx_bytes=tx + 6,
                            framed_rx_bytes=rx + 6, owned_values=values, checksum=checksum)
            # Required zero bytes break every COBS run well below 254 bytes;
            # unlike the generic capacity bound, actual framing adds six bytes.
            if any(mode.get(key) != value for key, value in expected.items()):
                raise RuntimeError('Mapping bytes/getters/values differ: ' + str((count, work, mode)))
            timing = mode.get('timing', {})
            if timing.get('iterations') != 200 or timing.get('framing_included') is not False:
                raise RuntimeError('Timing scope must exclude COBS accounting')
            for key in ('host_elapsed_ns', 'dispatcher_elapsed_ns'):
                if not isinstance(timing.get(key), int) or timing[key] < 0:
                    raise RuntimeError('Invalid elapsed-time observation')
            if timing.get('checksum') != (checksum * 200) & 0xffffffffffffffff:
                raise RuntimeError('Timed pass did not retain the selected values')
            service_copy = count * 4 if mode['name'] == 'fixed_service' else 0
            if (mode.get('retained_scalar_bytes') != count * 4 or
                    mode.get('explicit_service_pack_bytes') != service_copy or
                    mode.get('explicit_service_id_copy_bytes') != service_copy):
                raise RuntimeError('Explicit value-copy accounting differs')
        if modes[0].get('work_checksum') != modes[1].get('work_checksum'):
            raise RuntimeError('The two mappings did not retain the same getter work')


def arm_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm-only', action='store_true', required=True)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--cxx', default='arm-none-eabi-g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if args.jobs < 1 or out == ROOT or ROOT in out.parents or out in ROOT.parents:
        parser.error('Use positive jobs and an external output directory')
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        parser.error('Use a fresh empty output directory; nothing is deleted')
    out.mkdir(parents=True, exist_ok=True)
    tools = {name: str(Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute())
             for name in ('cxx', 'cmake', 'ninja')}
    for name in ('nm', 'size', 'objdump'):
        sibling = Path(tools['cxx']).with_name('arm-none-eabi-' + name)
        tools[name] = str(sibling if sibling.is_file() else Path(shutil.which(sibling.name) or sibling))
    report = dict(passed=False, hardware=False, arm_only=True,
                  started_utc=datetime.now(timezone.utc).isoformat(), commands=[], profiles=[])
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    env['PATH'] = str(Path(tools['cxx']).parent) + os.pathsep + env.get('PATH', '')

    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def inputs():
        result = {}
        for directory in (ROOT / 'lib', ROOT / 'cmake', ROOT / 'examples/read_many_mapping', HERE):
            for path in sorted(directory.rglob('*')):
                if path.is_file() and path.suffix in ('.hpp', '.h', '.cpp', '.py', '.txt', '.cmake', '.in'):
                    result[path.relative_to(ROOT).as_posix()] = digest(path)
        for path in (ROOT / 'CMakeLists.txt', ROOT / '.github/workflows/ci.yml',
                     ROOT / 'examples/read_comparison/DataModel.hpp'):
            result[path.relative_to(ROOT).as_posix()] = digest(path)
        return result

    def save():
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def run(name, command):
        start = time.monotonic()
        command = list(map(str, command))
        try:
            proc = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=300)
            output, code = proc.stdout, proc.returncode
        except subprocess.TimeoutExpired as error:
            output, code = error.stdout or b'', None
        log = out / (name + '.log')
        log.write_bytes(output)
        report['commands'].append(dict(name=name, command=command, exit=code,
                                       elapsed_seconds=time.monotonic() - start, sha256=digest(log)))
        save()
        print(name + ': exit ' + str(code), flush=True)
        if code != 0:
            raise RuntimeError(name + ' failed\n' + output.decode(errors='replace')[-5000:])
        return output.decode('utf-8')

    try:
        report['inputs_before'] = inputs()
        report['source_head'] = run('source-head', ['git', 'rev-parse', 'HEAD']).strip()
        for name, path in tools.items():
            run(name + '-version', [path, '--version'])
        report['tool_sha256'] = {name: digest(Path(path).resolve()) for name, path in tools.items()}
        for opt in ('Os', 'O2'):
            package, prefix, build = (out / (opt + '-' + n) for n in ('package', 'install', 'build'))
            flags = ('-mcpu=cortex-m7 -mthumb -' + opt + ' -ffunction-sections -fdata-sections '
                     '-fno-exceptions -fno-rtti -fstack-usage -DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0')
            common = ['-G', 'Ninja', '-DCMAKE_SYSTEM_NAME=Generic',
                      '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY', '-DCMAKE_BUILD_TYPE=Release',
                      '-DCMAKE_CXX_COMPILER=' + tools['cxx'], '-DCMAKE_MAKE_PROGRAM=' + tools['ninja'],
                      '-DCMAKE_CXX_FLAGS=' + flags, '-DCMAKE_CXX_FLAGS_RELEASE=-DNDEBUG',
                      '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
            run(opt + '-package-configure', [tools['cmake'], '-S', ROOT, '-B', package, *common,
                '-DBUILD_TESTING=OFF', '-DTELEMETRY_COMPONENTS=core;runtime;encoded_server',
                '-DCMAKE_INSTALL_PREFIX=' + str(prefix)])
            run(opt + '-package-build', [tools['cmake'], '--build', package, '-j', args.jobs])
            run(opt + '-package-install', [tools['cmake'], '--install', package])
            installed = {p.relative_to(prefix).as_posix(): digest(p) for p in prefix.rglob('*') if p.is_file()}
            run(opt + '-configure', [tools['cmake'], '-S', ROOT / 'examples/read_many_mapping', '-B', build,
                *common, '-DREAD_MANY_ARM_PROBE=ON', '-DTelemetry_DIR=' + str(prefix / 'lib/cmake/Telemetry')])
            run(opt + '-build', [tools['cmake'], '--build', build, '-j', args.jobs])
            commands = json.loads((build / 'compile_commands.json').read_text())
            if len(commands) != 2:
                raise RuntimeError('Expected exactly two selected roots')
            for command in commands:
                text = command['command'].replace('\\', '/').lower()
                if (ROOT / 'lib').as_posix().lower() in text or (prefix / 'include').as_posix().lower() not in text:
                    raise RuntimeError('ARM consumer escaped its installed prefix')
            for mapping in ('compact', 'service'):
                binary = build / ('read-many-' + mapping)
                linked = run(opt + '-' + mapping + '-commands', [tools['ninja'], '-C', build,
                             '-t', 'commands', 'read-many-' + mapping]).replace('\\', '/').lower()
                if (ROOT / 'lib').as_posix().lower() in linked or (prefix / 'lib').as_posix().lower() not in linked:
                    raise RuntimeError('ARM link escaped the installed archives')
                sizes = run(opt + '-' + mapping + '-size', [tools['size'], '-A', binary])
                symbols = run(opt + '-' + mapping + '-nm', [tools['nm'], '-S', '-C', binary])
                run(opt + '-' + mapping + '-objdump', [tools['objdump'], '-drwC', binary])
                if re.search(r'\b(malloc|calloc|realloc|_sbrk)\b|operator new', symbols):
                    raise RuntimeError('Heap symbol present in selected ARM image')
                if not re.search(r'\b[Tt]\s+read_many_' + mapping + r'_probe\b', symbols):
                    raise RuntimeError('Selected entry root was discarded')
                other = 'service' if mapping == 'compact' else 'compact'
                if re.search(r'\bread_many_' + other + r'_probe\b', symbols):
                    raise RuntimeError('Opposite mapping root leaked into the image')
                sections = {m[1]: int(m[2]) for m in re.finditer(r'^\s*(\.\S+)\s+(\d+)\s+\d+', sizes, re.M)}
                if not sections.get('.text'):
                    raise RuntimeError('Missing linked text')
                report['profiles'].append(dict(optimization=opt, mapping=mapping, sections=sections,
                    binary_sha256=digest(binary), compile_commands=commands,
                    note='Selected-root linked sections; not firmware Flash or call-chain peak stack'))
            usage = sorted(build.rglob('*.su')) + sorted(package.rglob('*.su'))
            if not usage:
                raise RuntimeError('No ARM stack-usage records')
            report[opt + '_stack_usage'] = {p.relative_to(out).as_posix(): digest(p) for p in usage}
            for profile in report['profiles']:
                if profile['optimization'] != opt:
                    continue
                root_name = 'read_many_' + profile['mapping'] + '_probe('
                records = [line.split('\t') for path in usage
                           for line in path.read_text().splitlines() if root_name in line]
                if len(records) != 1 or len(records[0]) != 3 or not records[0][1].isdigit():
                    raise RuntimeError('Missing unique entry stack-frame record')
                profile['entry_frame_bytes'] = int(records[0][1])
                profile['entry_frame_kind'] = records[0][2]
                profile['entry_frame_note'] = 'Individual compiler frame, not a call-chain peak'
            if installed != {p.relative_to(prefix).as_posix(): digest(p) for p in prefix.rglob('*') if p.is_file()}:
                raise RuntimeError('Installed ARM package changed')
            report[opt + '_installed'] = installed
        report['inputs_after'] = inputs()
        if report['inputs_before'] != report['inputs_after']:
            raise RuntimeError('ARM inputs changed during qualification')
        if report['source_head'] != run('source-head-after', ['git', 'rev-parse', 'HEAD']).strip():
            raise RuntimeError('Git identity changed')
        report['passed'] = True
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(error, flush=True)
    report['finished_utc'] = datetime.now(timezone.utc).isoformat()
    save()
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    if '--arm-only' in sys.argv:
        sys.exit(arm_main())
    shared = runpy.run_path(str(HERE.parent / 'read_comparison/run.py'))
    sys.exit(shared['main'](example='read_many_mapping', target='read-many-mapping',
                            validator=validate_mapping, report_name='mapping.json',
                            noheap='read-many-noheap',
                            components=('core', 'runtime', 'encoded_server', 'resource_protocol')))
