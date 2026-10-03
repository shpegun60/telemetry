#!/usr/bin/env python3
"""Compile and execute core-only, v2-only, v3-only and both qmake selections. MIT."""
import argparse
import os
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qmake', required=True)
    parser.add_argument('--make', default='make')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    for mode in ('core', 'v2', 'v3', 'both'):
        out = args.build_dir.resolve() / mode; out.mkdir(parents=True, exist_ok=True)
        commands = [[args.qmake, str(HERE / 'resources.pro'), 'MODE=' + mode,
                     'QMAKE_CXXFLAGS+=-Werror', 'QMAKE_CXXFLAGS+=-UNDEBUG'], [args.make, '-j2']]
        for index, cmd in enumerate(commands):
            result = subprocess.run(cmd, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=300)
            (out / f'{index}.log').write_text(result.stdout, encoding='utf-8')
            if result.returncode: raise RuntimeError(f'{mode}: {cmd}\n{result.stdout}')
        candidates = [out / 'structured_resources', out / 'release/structured_resources.exe', out / 'structured_resources.exe']
        program = next(p for p in candidates if p.is_file())
        subprocess.run([str(program)], cwd=out, check=True, timeout=30)
        makefiles = '\n'.join(p.read_text(errors='replace') for p in out.glob('Makefile*'))
        assert 'structured_protocol' not in makefiles
        assert 'Bind.cpp' not in makefiles and 'Exchange.cpp' not in makefiles
        if mode in ('core', 'v2'):
            # qmake's dependency scanner also lists includes in inactive #ifdef
            # branches. Check actual compiler paths and linked objects instead.
            paths = '\n'.join(re.findall(r'^INCPATH\s*=.*$', makefiles, re.M))
            assert 'boost_pfr' not in paths
            assert 'structured/detail/Values.cpp' not in (out / '1.log').read_text().replace('\\', '/')
        if mode in ('core', 'v3'):
            assert 'telemetry/serialization/TelemetryJson.cpp' not in makefiles.replace('\\', '/')
        print(mode + ': build/run passed', flush=True)


if __name__ == '__main__':
    main()
