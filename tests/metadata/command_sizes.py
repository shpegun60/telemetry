#!/usr/bin/env python3
"""Compare isolated command metadata implementations on ARM; not cycle evidence.

Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BINDING = Path("telemetry/detail/TelemetryCommandBinding.h")
LIMITS = Path("telemetry/field/TelemetryLimits.h")
PARAMETER = "    static constexpr CommandParam parameter(const Metadata* metadata) noexcept"
REFINE = "constexpr FieldType refineType(ValueLimits<U, Bounded> values) noexcept"


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError("Source anchor changed: " + old)
    return text.replace(old, new, 1)


def fixture(count):
    types = ", ".join("float" if i % 2 == 0 else "Mode" for i in range(count))
    args = ", ".join(
        f'telemetry::arg<{i}>("Limit", "V", 230.f, 0.f, 300.f)' if i % 2 == 0 else
        f'telemetry::arg<{i}>("Mode", "", Mode::Auto)' for i in range(count))
    return ('#include <telemetry/Telemetry.h>\n'
            'enum class Mode : std::uint16_t { Off, Auto, Manual };\n'
            f'telemetry::CommandResult target({types}) noexcept '
            '{ return telemetry::CommandResult::Executed; }\n'
            f'constexpr telemetry::CommandTable table{{telemetry::command<&target>("Configure", {args})}};\n'
            'extern "C" const telemetry::Command* metadata_command() noexcept { return table.data(); }\n'
            'extern "C" bool metadata_visit(void* context, telemetry::CommandParamSink sink) noexcept '
            '{ return table[0].describeParameters(context, sink); }\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    compiler = Path(shutil.which(args.cxx) or args.cxx).resolve()
    suffix = compiler.suffix
    size = compiler.with_name("arm-none-eabi-size" + suffix)
    objdump = compiler.with_name("arm-none-eabi-objdump" + suffix)
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    binding = (ROOT / "lib" / BINDING).read_text(encoding="utf-8")
    limits = (ROOT / "lib" / LIMITS).read_text(encoding="utf-8")
    # Normalize the baseline explicitly so the experiment remains reproducible
    # when the selected inlining policy changes in the production headers.
    binding = binding.replace(PARAMETER.replace("static constexpr", "TELEMETRY_FORCE_INLINE static constexpr"), PARAMETER)
    limits = limits.replace("TELEMETRY_FORCE_INLINE " + REFINE, REFINE)
    inlined = replace_once(binding, PARAMETER, PARAMETER.replace("static constexpr", "TELEMETRY_FORCE_INLINE static constexpr"))
    separate = replace_once(binding, "template <std::size_t I>\n" + PARAMETER,
                            "template <std::size_t I, bool Indexed = false>\n" + PARAMETER)
    separate = replace_once(separate, "parameter<I>(static_cast<const Metadata*>(metadata))",
                            "parameter<I, true>(static_cast<const Metadata*>(metadata))")
    old = ("        static constexpr auto entries = parameterEntries(std::make_index_sequence<Traits::arity>{});\n"
           "        return sink != nullptr && index < entries.size() && entries[index](metadata, context, sink);")
    # This is an experiment for signatures up to 16 arguments, not a proposed
    # public API cap. Generate actual switch cases instead of assuming that a
    # recursive if-chain will be converted to a jump table.
    cases = "\n".join(f"        case {i}: if constexpr (Traits::arity > {i}) "
                      f"return parameterEntry<{i}>(metadata, context, sink); else return false;"
                      for i in range(16))
    switch = replace_once(binding, old, "        if (sink == nullptr) return false;\n"
                           "        switch (index) {\n" + cases + "\n        default: return false;\n        }")
    variants = {
        "table": (binding, limits),
        "inline_parameter": (inlined, limits),
        "separate": (separate, limits),
        "switch": (switch, limits),
        "inline_factory": (inlined, replace_once(limits, REFINE, "TELEMETRY_FORCE_INLINE " + REFINE)),
    }
    rows = []
    hashes = {}
    for name, (command_header, limits_header) in variants.items():
        tree = out / name
        for library in ("telemetry", "delegate", "magic_enum"):
            shutil.copytree(ROOT / "lib" / library, tree / "lib" / library, dirs_exist_ok=True)
        (tree / "lib" / BINDING).write_text(command_header, encoding="utf-8")
        (tree / "lib" / LIMITS).write_text(limits_header, encoding="utf-8")
        hashes[name] = {str(p.relative_to(tree)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in (tree / "lib" / BINDING, tree / "lib" / LIMITS)}
        for count in (2, 8, 16):
            cpp = tree / f"Probe{count}.cpp"
            cpp.write_text(fixture(count), encoding="utf-8")
            for opt in ("O2", "Os"):
                obj = tree / f"{opt}-{count}.o"
                flags = ["-std=c++17", "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
                         "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
                         "-Wall", "-Wextra", "-Werror", "-fstack-usage", "-" + opt,
                         "-I" + str(tree / "lib"), "-I" + str(tree / "lib/delegate")]
                command = [str(compiler), *flags, "-c", str(cpp), "-o", str(obj)]
                result = subprocess.run(command, capture_output=True, text=True)
                obj.with_suffix(".log").write_text(repr(command) + "\n" + result.stdout + result.stderr, encoding="utf-8")
                if result.returncode:
                    raise RuntimeError(result.stderr)
                asm = subprocess.check_output([str(objdump), "-drC", str(obj)], text=True)
                obj.with_suffix(".asm").write_text(asm, encoding="utf-8")
                sizes = subprocess.check_output([str(size), "-A", str(obj)], text=True)
                obj.with_suffix(".sections").write_text(sizes, encoding="utf-8")
                sections = {s: sum(int(line.split()[1]) for line in sizes.splitlines() if line.split()
                                   and (line.split()[0] == "." + s or line.split()[0].startswith("." + s + ".")))
                            for s in ("text", "rodata", "data")}
                rows.append(dict(variant=name, count=count, opt=opt, **sections, total=sum(sections.values())))
        print(name, "done", flush=True)
    report = {"compiler": subprocess.check_output([str(compiler), "--version"], text=True).splitlines()[0],
              "headers": hashes, "measurements": rows}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for row in rows:
        print(row)


if __name__ == "__main__":
    main()
