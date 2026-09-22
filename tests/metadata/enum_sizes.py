#!/usr/bin/env python3
"""Compare complete enum ops (sequential + indexed) ARM object sections, not cycles."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def source():
    # Generate a real switch, rather than assuming a recursive if chain becomes
    # one. All alternatives retain the same sequential fold and sink contract.
    codes = ', '.join(f'V{i}' for i in range(64))
    cases = '\n'.join(f'case {i}: if constexpr (N > {i}) return emit<{i}>(ctx, sink); else return false;'
                      for i in range(64))
    return '''#include "Telemetry.h"
using namespace telemetry;
#if WIDE
using Raw = std::uint64_t;
#else
using Raw = std::uint16_t;
#endif
enum class E : Raw { ''' + codes + ''' };
template<std::size_t I> bool emit(void* ctx, EnumEntrySink sink) noexcept {
    return detail::emitEnumEntry<E, static_cast<E>(I)>(ctx, sink);
}
template<class> struct Fixture;
template<std::size_t... I> struct Fixture<std::index_sequence<I...>> {
    static constexpr auto N = sizeof...(I);
    static bool sequential(void* ctx, EnumEntrySink sink) noexcept {
        return (emit<I>(ctx, sink) && ...);
    }
    static bool at(std::uint32_t index, void* ctx, EnumEntrySink sink) noexcept {
        if (index >= N || sink == nullptr) return false;
#if METHOD == 0
        using Emit = bool(*)(void*, EnumEntrySink) noexcept;
        static constexpr std::array<Emit, N> table{&emit<I>...};
        return table[index](ctx, sink);
#elif METHOD == 1
        switch(index) { ''' + cases + ''' default: return false; }
#else
        struct Entry { Raw code; std::string_view name; };
        static constexpr std::array<Entry, N> table{{
            {static_cast<Raw>(I), magic_enum::enum_name<static_cast<E>(I)>()}...
        }};
        const auto& item = table[index];
        const Scalar value = Scalar::from(item.code);
        return sink(ctx, value, item.name);
#endif
    }
};
using Selected = Fixture<std::make_index_sequence<COUNT>>;
extern const EnumOps measuredOps;
const EnumOps measuredOps{COUNT, &Selected::sequential, &Selected::at};
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cxx', required=True)
    p.add_argument('--size', required=True)
    p.add_argument('--build-dir', type=Path, required=True)
    args = p.parse_args()
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cpp = out / 'EnumOps.cpp'
    cpp.write_text(source())
    results = []
    for opt in ('O2', 'Os'):
        for count in (3, 16, 64):
            for wide in (0, 1):
                for method, name in enumerate(('function_table', 'switch', 'data_table')):
                    obj = out / f'{opt}-{count}-{wide}-{name}.o'
                    subprocess.run([args.cxx, '-std=c++17', '-mcpu=cortex-m7', '-mthumb',
                        '-mfpu=fpv5-d16', '-mfloat-abi=hard', '-fno-exceptions', '-fno-rtti',
                        '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra', '-Werror',
                        '-Ilib/telemetry', '-Ilib/delegate', '-'+opt,
                        f'-DMETHOD={method}', f'-DCOUNT={count}', f'-DWIDE={wide}',
                        '-c', str(cpp), '-o', str(obj)], cwd=ROOT, check=True)
                    sizes = subprocess.check_output([args.size, '-A', str(obj)], text=True)
                    (obj.with_suffix('.sections')).write_text(sizes)
                    sections = {'text': 0, 'rodata': 0, 'data': 0}
                    for line in sizes.splitlines():
                        cells = line.split()
                        for section in sections:
                            if cells and (cells[0] == '.'+section or cells[0].startswith('.'+section+'.')):
                                sections[section] += int(cells[1])
                    results.append(dict(opt=opt, count=count, bits=64 if wide else 16,
                                        method=name, **sections, total=sum(sections.values())))
            print(opt, count, 'measured', flush=True)
    (out/'results.json').write_text(json.dumps(results, indent=2)+'\n')
    for row in results:
        print(row)


if __name__ == '__main__':
    main()
