"""Bound individual generic resource frames; nested peak is a separate gate.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
FRAME_LIMIT = 192


def check_usage(text, required=None, optimization=None):
    frames = []
    for line in text.splitlines():
        if not line.strip():
            continue
        parts = line.rsplit('\t', 2)
        if len(parts) != 3 or not parts[0] or not parts[1].isdigit():
            raise RuntimeError('Malformed stack-usage entry: ' + line)
        name, amount, kind = parts
        if kind not in ('static', 'dynamic,bounded') or int(amount) > FRAME_LIMIT:
            raise RuntimeError('Unbounded or excessive resource frame: ' + line)
        frames.append({'function': name, 'bytes': int(amount), 'kind': kind, 'limit': FRAME_LIMIT})
    if not frames or (required and not any(required in frame['function'] for frame in frames)):
        raise RuntimeError('Missing stack report or required function')
    return {'maximum': max(frame['bytes'] for frame in frames), 'frames': frames}


def self_test():
    good = 'probe.cpp:1:1:resource_protocol::process()\t192\tstatic'
    if check_usage(good, 'process')['maximum'] != 192:
        raise RuntimeError('Resource stack positive control failed')
    for text, required in (('', None), ('not a report', None),
                           (good.replace('192', '193'), None),
                           (good.replace('192', '-1'), None),
                           (good.replace('static', 'dynamic'), None),
                           (good.replace('static', 'static,ignoring_inline_asm'), None),
                           (good, 'missing')):
        try:
            check_usage(text, required)
        except RuntimeError:
            continue
        raise RuntimeError('Resource stack mutation accepted')


if __name__ == '__main__':
    self_test()
    print('Generic resource stack controls passed')
