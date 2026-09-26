#!/usr/bin/env python3
"""Attribute linked .text/.rodata bytes by symbol family, counting aliases once."""
# Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import subprocess


def family(name):
    # Order matters: a ValuesFile constructor also names SchemaFile's type.
    if name.startswith('telemetry_resource::ValuesFile::') or 'ValuesFile::read(' in name:
        return 'Values provider'
    if 'SchemaFile::' in name:
        return 'Schema provider and callbacks'
    if 'CommandsFile::' in name:
        return 'Commands provider and callbacks'
    if 'telemetry_resource::' in name:
        return 'Shared binary and cursor helpers'
    if re.search(r'IndexedEnum|emitEnum|enumDescription|describeEnum|parameterOps|parameterEntr|'
                 r'describeParameter|Command(?:Contract|Binding).*::schema\(', name):
        return 'Enum and command metadata operations'
    if 'resource_protocol::' in name:
        return 'Resource protocol'
    if 'telemetry::' in name:
        return 'Other telemetry helpers'
    if name.startswith('demo::') or name.startswith('device::'):
        return 'Application facade and catalogs'
    return 'Other symbols and runtime support'


def inspect(elf, nm, size):
    section_text = subprocess.check_output([size, '-A', str(elf)], text=True)
    sections = {}
    for line in section_text.splitlines():
        match = re.fullmatch(r'(\.text|\.rodata)\s+(\d+)\s+(\d+)\s*', line)
        if match:
            sections[match[1]] = {'size': int(match[2]), 'address': int(match[3])}
    if set(sections) != {'.text', '.rodata'}:
        raise ValueError('Expected linked .text and .rodata sections')
    text = subprocess.check_output([nm, '-S', '--defined-only', '-C', str(elf)], text=True)
    symbols, names, occupied = [], defaultdict(list), {}
    for line in text.splitlines():
        match = re.fullmatch(r'([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\S\s+(.*)', line)
        if not match:
            continue
        address, length, name = int(match[1], 16), int(match[2], 16), match[3]
        section = next((s for s, bounds in sections.items()
                        if bounds['address'] <= address < bounds['address'] + bounds['size']), None)
        if section is None or length == 0:
            continue
        if address + length > sections[section]['address'] + sections[section]['size']:
            raise ValueError('Symbol exceeds section: ' + name)
        category = family(name)
        for byte in range(address, address + length):
            if byte in occupied and occupied[byte] != category:
                raise ValueError('Overlapping symbols assigned different families: ' + name)
            occupied[byte] = category
        symbols.append({'address': address, 'bytes': length, 'section': section,
                        'family': category, 'name': name})
        names[(address, length)].append(name)
    totals = defaultdict(int)
    for category in occupied.values():
        totals[category] += 1
    total = sum(s['size'] for s in sections.values())
    # Unnamed constants, strings and alignment are real bytes, not invented
    # function growth. Keep them explicit instead of assigning a guessed owner.
    totals['Unnamed data and alignment'] = total - len(occupied)
    return {'sha256': hashlib.sha256(elf.read_bytes()).hexdigest(), 'sections': sections,
            'text_rodata': total, 'families': dict(sorted(totals.items())),
            'symbols': symbols, 'alias_ranges': sum(len(n) > 1 for n in names.values())}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path, required=True)
    p.add_argument('--nm', required=True)
    p.add_argument('--size', required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    before = inspect(args.before, args.nm, args.size)
    after = inspect(args.after, args.nm, args.size)
    deltas = {name: after['families'].get(name, 0) - before['families'].get(name, 0)
              for name in sorted(before['families'].keys() | after['families'].keys())}
    if sum(deltas.values()) != after['text_rodata'] - before['text_rodata']:
        raise ValueError('Attribution does not reconcile with complete section sizes')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'before': before, 'after': after, 'deltas': deltas}, indent=2)
                           + '\n', encoding='utf-8')
    print(json.dumps({'before': before['text_rodata'], 'after': after['text_rodata'],
                      'delta': after['text_rodata'] - before['text_rodata'], 'families': deltas}, indent=2))


if __name__ == '__main__':
    main()
