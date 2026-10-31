#!/usr/bin/env python3
"""Qualify installed read-comparison accounting without a board or Qt. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
COMPONENTS = ('dynamic_client', 'encoded_backend', 'encoded_server', 'core',
              'resource_adapter', 'resource_protocol')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def files_below(directory, suffixes):
    for base, directories, names in os.walk(directory):
        directories[:] = [name for name in directories
                          if name not in {'build', 'out', '__pycache__', '.git', 'tests'}]
        for name in names:
            if Path(name).suffix in suffixes:
                yield Path(base) / name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--cobs-root', type=Path, required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    out, cobs = args.build_dir.resolve(), args.cobs_root.resolve()
    if args.jobs < 1:
        parser.error('jobs must be positive')
    if not (cobs / 'src/cobs/Cobs.h').is_file():
        parser.error('cobs-root must contain src/cobs/Cobs.h')
    if any(out == source or source in out.parents or out in source.parents
           for source in (ROOT, cobs)):
        parser.error('build-dir must be outside source trees and their parents')
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        parser.error('build-dir must be fresh and empty; no files are deleted')
    out.mkdir(parents=True, exist_ok=True)
    # Retain the invoked clang++ driver name; resolving it to clang changes
    # the link driver's language and can omit the C++ runtime libraries.
    tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
             for name in ('cxx', 'cmake', 'ninja')}
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    env['PATH'] = str(tools['cxx'].parent) + os.pathsep + env.get('PATH', '')
    if args.sanitize:
        env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')

    def inputs():
        suffixes = {'.cpp', '.c', '.hpp', '.h', '.txt', '.cmake', '.in', '.py', '.yml'}
        paths = set()
        for directory in (ROOT / 'lib', ROOT / 'cmake', ROOT / 'examples/read_comparison',
                          HERE, cobs / 'src/cobs', cobs / 'src/wire', cobs / 'src/crc'):
            paths.update(files_below(directory, suffixes))
        paths.update((ROOT / 'CMakeLists.txt', ROOT / '.github/workflows/ci.yml',
                      ROOT / 'examples/inspector_serial/SerialProfile.hpp'))
        result = {}
        for path in sorted(paths):
            if path.is_relative_to(ROOT):
                name = path.relative_to(ROOT).as_posix()
            else:
                name = 'COBS_ROOT/' + path.relative_to(cobs).as_posix()
            result[name] = digest(path)
        return result

    report = dict(passed=False, hardware=False, sanitize=args.sanitize, jobs=args.jobs,
                  started_utc=datetime.now(timezone.utc).isoformat(), commands=[],
                  source_sha256_before=inputs(), selected_components=list(COMPONENTS),
                  external_cobs=str(cobs), installed_consumer=True,
                  timing_thresholds=False)
    if args.sanitize:
        report.update(sanitizers=['address', 'undefined'], leak_checking=True)

    def save():
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def run(name, command, cwd=ROOT, timeout=300):
        started = time.monotonic()
        command = list(map(str, command))
        try:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=timeout)
            output, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            output, code = error.stdout or b'', None
        log = out / (name + '.log')
        log.write_bytes(output)
        report['commands'].append(dict(name=name, command=command, exit=code,
                                       seconds=time.monotonic() - started,
                                       log=log.name, sha256=digest(log)))
        save()
        print(name + ': exit ' + str(code), flush=True)
        if code != 0:
            raise RuntimeError(name + (' timed out' if code is None else ' failed') + '\n' +
                               output.decode('utf-8', errors='replace')[-10000:])
        return output.decode('utf-8', errors='strict')

    try:
        for name, tool in tools.items():
            if not tool.is_file():
                raise RuntimeError('Missing tool: ' + str(tool))
            run(name + '-version', [tool, '--version'])
        report['tool_sha256'] = {str(path): digest(path.resolve()) for path in tools.values()}
        report['python_version'] = sys.version
        report['python_executable'] = sys.executable
        report['source_head'] = run('source-head', ['git', 'rev-parse', 'HEAD']).strip()
        report['source_status_before'] = run('source-status-before', ['git', 'status', '--porcelain'])
        report['cobs_head'] = run('cobs-head', ['git', 'rev-parse', 'HEAD'], cwd=cobs).strip()
        report['cobs_status_before'] = run('cobs-status-before', ['git', 'status', '--porcelain'], cwd=cobs)
        flags = '-Wall -Wextra -Werror -fstack-usage -DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0'
        if args.sanitize:
            flags += ' -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
        package_build, prefix, build = out / 'package-build', out / 'install', out / 'build'
        common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                  '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']),
                  '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
                  '-DCMAKE_CXX_FLAGS=' + flags, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        if args.sanitize:
            common += ['-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG',
                       '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run('package-configure', [tools['cmake'], '-S', ROOT, '-B', package_build,
                                 *common, '-DBUILD_TESTING=OFF',
                                 '-DTELEMETRY_COMPONENTS=' + ';'.join(COMPONENTS),
                                 '-DCMAKE_INSTALL_PREFIX=' + str(prefix)])
        run('package-build', [tools['cmake'], '--build', package_build, '-j', args.jobs])
        run('package-install', [tools['cmake'], '--install', package_build])
        installed_before = {path.relative_to(prefix).as_posix(): digest(path)
                            for path in sorted(prefix.rglob('*')) if path.is_file()}
        config = prefix / 'lib/cmake/Telemetry'
        if not (config / 'TelemetryConfig.cmake').is_file():
            raise RuntimeError('Installed TelemetryConfig.cmake is missing')
        run('configure', [tools['cmake'], '-S', ROOT / 'examples/read_comparison', '-B', build,
                          *common, '-DTelemetry_DIR=' + str(config), '-DCOBS_ROOT=' + str(cobs)])
        run('build', [tools['cmake'], '--build', build, '-j', args.jobs])
        binary = build / ('read-comparison.exe' if os.name == 'nt' else 'read-comparison')
        run('runtime', [binary, '--report', out / 'comparison.json'])
        comparison = json.loads((out / 'comparison.json').read_text(encoding='utf-8'))
        validate_comparison(comparison)
        report['comparison_sha256'] = digest(out / 'comparison.json')
        report['executions'] = comparison
        report['binary_sha256'] = {str(binary.relative_to(out)): digest(binary)}
        usage = sorted(path for directory in (package_build, build) for path in directory.rglob('*.su'))
        if not usage:
            raise RuntimeError('Compiler stack-usage files are missing')
        report['stack_usage_sha256'] = {path.relative_to(out).as_posix(): digest(path) for path in usage}
        report['installed_sha256'] = {path.relative_to(prefix).as_posix(): digest(path)
                                      for path in sorted(prefix.rglob('*')) if path.is_file()}
        if installed_before != report['installed_sha256']:
            raise RuntimeError('Installed package changed during consumer qualification')
        report['installed_package_before_after_equal'] = True
        compile_commands = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
        source_lib = (ROOT / 'lib').as_posix().casefold()
        installed_include = (prefix / 'include').as_posix().casefold()
        proof = []
        for entry in compile_commands:
            command = entry.get('command', ' '.join(entry.get('arguments', [])))
            normalized = command.replace('\\', '/').casefold()
            if source_lib in normalized:
                raise RuntimeError('Checkout lib headers leaked into the standalone consumer')
            if Path(entry['file']).name == 'Host.cpp':
                if installed_include not in normalized:
                    raise RuntimeError('Host.cpp did not use the installed module include paths')
                proof.append(entry)
        if len(proof) != 1:
            raise RuntimeError('Missing unique Host.cpp installed-consumer compile proof')
        linked = run('target-commands', [tools['ninja'], '-C', build, '-t', 'commands', 'read-comparison'])
        if source_lib in linked.replace('\\', '/').casefold():
            raise RuntimeError('Checkout lib sources leaked into the standalone target')
        if (prefix / 'lib').as_posix().casefold() not in linked.replace('\\', '/').casefold():
            raise RuntimeError('Installed archives missing from the standalone link command')
        report['installed_package_proof'] = dict(cmake_dir=str(config), no_checkout_lib_inputs=True,
                                                  compile_proof=proof)
        report['source_sha256_after'] = inputs()
        report['source_head_after'] = run('source-head-after', ['git', 'rev-parse', 'HEAD']).strip()
        report['source_status_after'] = run('source-status-after', ['git', 'status', '--porcelain'])
        report['cobs_status_after'] = run('cobs-status-after', ['git', 'status', '--porcelain'], cwd=cobs)
        if report['source_sha256_before'] != report['source_sha256_after']:
            raise RuntimeError('Qualification inputs changed during execution')
        if report['source_head'] != report['source_head_after']:
            raise RuntimeError('Git identity changed during execution')
        report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(str(error), flush=True)
    report['finished_utc'] = datetime.now(timezone.utc).isoformat()
    save()
    return 0 if report['passed'] else 1


def validate_comparison(report):
    """Independent deterministic checks; no elapsed-time acceptance threshold."""
    if not report.get('passed') or report.get('hardware') is not False:
        raise RuntimeError('Example did not confirm the offline correctness checks')
    for flag in ('wire_batch_implemented', 'coherent_snapshot'):
        if report.get(flag) is not False:
            raise RuntimeError('Example incorrectly claims ' + flag)
    if report.get('host_timing_excludes_framing') is not True:
        raise RuntimeError('Host timings must exclude the separate framing accounting pass')
    profiles = report.get('profiles')
    if not isinstance(profiles, list) or len(profiles) != 2:
        raise RuntimeError('Both all8 and sparse8of32 profiles must run')
    if {profile.get('name') for profile in profiles} != {'all8', 'sparse8of32'}:
        raise RuntimeError('Unexpected profile names')
    controls = report.get('framing_controls', {})
    if controls != dict(empty=True, max=True, oversize=True, roundtrip=True, cases=6):
        raise RuntimeError('Framing boundary and round-trip controls did not run')
    if not isinstance(report.get('checks'), int) or report['checks'] < 1:
        raise RuntimeError('Example assertion count is missing')
    if not isinstance(report.get('timing_iterations'), int) or report['timing_iterations'] < 1:
        raise RuntimeError('Example timing iteration count is missing')
    modes_expected = {'individual', 'aggregate', 'values_cold', 'values_warm',
                      'values_paged_cold', 'values_paged_warm'}
    for profile in profiles:
        count, stride = (8, 1) if profile['name'] == 'all8' else (32, 4)
        ids = [index * stride for index in range(8)]
        values = [0x10203040 ^ (index * 0x01010101) for index in ids]
        values = [0 if index == 0 else 0xffffffff if index == 7 else value
                  for index, value in zip(ids, values)]
        if (profile.get('field_count') != count or profile.get('requested_ids') != ids or
                profile.get('expected_values') != values):
            raise RuntimeError('Wrong immutable Model selection: ' + profile['name'])
        expected_checksum = 0
        for value in values:
            expected_checksum = (expected_checksum * 33 + value) & 0xffffffffffffffff
        if profile.get('memory', {}).get('owned_array_bytes') != 32:
            raise RuntimeError('Owned selection must contain exactly eight uint32 values')
        fingerprints = [profile.get(key, '') for key in
                        ('scalar_descriptor_fingerprint', 'aggregate_descriptor_fingerprint')]
        if (not all(re.fullmatch(r'[0-9a-fA-F]{16}', item) for item in fingerprints) or
                fingerprints[0] == fingerprints[1] or
                profile.get('different_descriptor_projection') is not True):
            raise RuntimeError('The aggregate must have an explicitly different Descriptor projection')
        modes = profile.get('modes', [])
        if len(modes) != 6 or {mode.get('name') for mode in modes} != modes_expected:
            raise RuntimeError('All six read paths must run in each profile')
        checksums = set()
        for mode in modes:
            name = mode['name']
            if mode.get('correct') is not True or mode.get('owned_values') != values:
                raise RuntimeError('Owned values differ for ' + profile['name'] + '/' + name)
            if mode.get('checksum') != expected_checksum:
                raise RuntimeError('Selected-value checksum differs from the immutable inputs')
            if name == 'individual':
                calls, exchanges, callbacks, returned, capacity, cursor = 8, 8, 8, 8, 0, 0
                tx, rx = 8 * 32, 8 * 36
            elif name == 'aggregate':
                calls, exchanges, callbacks, returned, capacity, cursor = 1, 1, 1, 8, 0, 0
                tx, rx = 32, 32 + 8 * 4
            else:
                warm, paged = name.endswith('_warm'), '_paged_' in name
                calls, callbacks, returned = count, 0, count
                capacity, cursor = (32 if paged else 2036), (24 if warm else 0)
                # ValuesFile emits whole tokens: header24 + one5-byte scalar
                # on the first cold32-byte page; later pages fit six scalars.
                exchanges = ((count + 5) // 6 if warm else 1 + (count - 1 + 5) // 6) if paged else 1
                tx = exchanges * 13
                rx = exchanges * 12 + 5 * count + (0 if warm else 24)
            expected = dict(getter_calls=calls, exchanges=exchanges, callbacks=callbacks,
                            protocol_tx_bytes=tx, protocol_rx_bytes=rx,
                            returned_scalars=returned, selected_scalars=8,
                            resource_payload_capacity=capacity, warm_cursor=cursor)
            # Every working protocol body here is below249 bytes. Its actual
            # two-byte-length/CRC16 COBS frame has exactly six extra bytes;
            # the separate boundary controls also exercise longer code runs.
            expected.update(framed_tx_bytes=tx + 6 * exchanges,
                            framed_rx_bytes=rx + 6 * exchanges)
            if any(mode.get(key) != value for key, value in expected.items()):
                raise RuntimeError('Deterministic traffic/count mismatch for ' +
                                   profile['name'] + '/' + name + ': ' + str(expected))
            framed_total = expected['framed_tx_bytes'] + expected['framed_rx_bytes']
            airtime = {str(baud): (framed_total * 10000000000 + baud - 1) // baud
                       for baud in (1200, 115200, 4000000)}
            if mode.get('uart_airtime_ns') != airtime:
                raise RuntimeError('8N1 UART airtime arithmetic mismatch')
            setup = mode.get('warm_setup')
            if name.endswith('_warm'):
                required_setup = dict(getter_calls=0, exchanges=1, callbacks=0,
                                      protocol_tx_bytes=13, protocol_rx_bytes=36,
                                      framed_tx_bytes=19, framed_rx_bytes=42)
                if not isinstance(setup, dict) or any(setup.get(k) != v for k, v in required_setup.items()):
                    raise RuntimeError('Warm header/fingerprint setup was not accounted separately')
            elif setup is not None:
                raise RuntimeError('Unexpected warm setup on a cold/Encoded mode')
            timing = mode.get('timing', {})
            if (timing.get('iterations') != report['timing_iterations'] or
                    timing.get('framing_included') is not False or
                    timing.get('poison_included') is not False or
                    timing.get('clock') != 'std::chrono::steady_clock' or
                    timing.get('checksum') != ((expected_checksum * report['timing_iterations']) &
                                               0xffffffffffffffff) or
                    not all(isinstance(timing.get(key), int) and timing[key] >= 0
                            for key in ('host_elapsed_ns', 'dispatcher_elapsed_ns'))):
                raise RuntimeError('Host wall-clock measurement scope is missing')
            checksums.add(mode.get('checksum'))
        if len(checksums) != 1 or None in checksums:
            raise RuntimeError('Read paths disagree on the selected-value checksum')
    for value in walk_numbers(report):
        if not math.isfinite(value) or value < 0:
            raise RuntimeError('Metrics contain a negative or non-finite number')


def walk_numbers(value):
    if isinstance(value, dict):
        for child in value.values():
            yield from walk_numbers(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk_numbers(child)
    elif isinstance(value, (int, float)) and not isinstance(value, bool):
        yield value


if __name__ == '__main__':
    sys.exit(main())
