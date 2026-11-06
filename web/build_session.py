#!/usr/bin/env python3
"""Strictly emit the single Session TypeScript source outside the checkout. MIT."""
import argparse
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--node', required=True)
    parser.add_argument('--tsc', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--update', action='store_true')
    args = parser.parse_args()
    output = Path(args.output).resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('compilation output must be outside the repository')
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.node, args.tsc, str(HERE / 'Session.ts'), '--strict',
                    '--noUnusedLocals', '--noUnusedParameters', '--target', 'ES2022',
                    '--module', 'ES2022', '--declaration', '--outDir', str(output)], check=True)
    # Imports of the existing Client.ts cause its independent output to be emitted
    # beneath lib/client/web as well. Only Session artifacts are ever published.
    for extension in ('js', 'd.ts'):
        emitted = output / 'web' / ('Session.' + extension)
        published = HERE / ('Session.' + extension)
        canonical = emitted.read_text(encoding='utf8').replace('\r\n', '\n').encode('utf8')
        if args.update:
            published.write_bytes(canonical)
        elif not published.exists() or published.read_bytes() != canonical:
            raise RuntimeError('emitted Session artifact is stale: ' + str(published))
    print('Session TypeScript: strict JS/declarations match (LF); Client artifacts unchanged')


if __name__ == '__main__':
    main()
