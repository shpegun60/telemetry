#!/usr/bin/env python3
"""Offline controls for the internal-Flash ELF load-span check (MIT)."""
from build import check_flash_sections


def section(name, size, vma, lma, flags='CONTENTS, ALLOC, LOAD, READONLY, CODE'):
    return f'  0 {name} {size:08x} {vma:08x} {lma:08x} 00001000 2**2\n {flags}\n'


def main():
    vector = section('.isr_vector', 0x100, 0x08000000, 0x08000000)
    data = section('.data', 0x20, 0x24000000, 0x08000100)
    bss = section('.bss', 0x100000, 0x24000020, 0x24000020, 'ALLOC')
    assert check_flash_sections(vector + data + bss, 0x120) == [0x08000000, 0x08000120]
    refused = (
        (section('.isr_vector', 0x100, 0x90000000, 0x90000000), 0x100),
        (vector + section('.text', 0x10000, 0x08000100, 0x08000100), 0x10100),
        (data, 0x20),
        (vector + data, 0x121),
        ('', 1),
    )
    for text, binary_bytes in refused:
        try:
            check_flash_sections(text, binary_bytes)
        except RuntimeError:
            continue
        raise AssertionError('Invalid ELF load span accepted')
    print(f'Flash-span controls passed: 1 positive, {len(refused)} refusals; no devices accessed')


if __name__ == '__main__':
    main()
