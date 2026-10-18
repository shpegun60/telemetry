#!/usr/bin/env python3
"""Compile the single TypeScript source externally; verify or update emitted distribution.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--node', required=True)
    parser.add_argument('--tsc', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--update', action='store_true', help='publish JS/declarations after strict compilation')
    args = parser.parse_args()
    output = Path(args.output).resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('compilation output must be outside the repository')
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.node, args.tsc, str(HERE / 'Client.ts'), '--strict', '--noUnusedLocals',
                    '--noUnusedParameters', '--target', 'ES2022', '--module', 'ES2022',
                    '--declaration', '--outDir', str(output)], check=True)
    for extension in ('js', 'd.ts'):
        emitted, published = output / ('Client.' + extension), HERE / ('Client.' + extension)
        if args.update:
            published.write_bytes(emitted.read_bytes())
        elif not published.exists() or published.read_bytes() != emitted.read_bytes():
            raise RuntimeError('emitted runtime is stale: ' + str(published))
    print('single-source TypeScript runtime: emitted JS/declarations match')


if __name__ == '__main__':
    main()
