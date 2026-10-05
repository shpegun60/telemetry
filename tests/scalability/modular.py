#!/usr/bin/env python3
"""Build the modular typed-header/erased-boundary fixture and inspect dependency fanout.

All generated fixtures, objects and logs live in an explicit external directory.
Smoke mode uses three consumers. Larger timed runs require an explicit --measure
invocation; timings describe this machine and compiler, never MCU execution.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_suffix("")
SUPPORT = (
    "lib/telemetry/abi/StructuredAbi.cpp",
    "lib/telemetry/model/Adapter.cpp",
    "examples/structured_protocol/Bind.cpp",
    "examples/structured_protocol/Exchange.cpp",
)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dependencies(path):
    """Read compiler -MMD output, including escaped Windows drive paths."""
    text = path.read_text(encoding="utf-8").replace("\\\n", " ")
    _, _, prerequisites = text.partition(": ")
    words = re.findall(r"(?:\\ |[^\s])+", prerequisites)
    return [Path(word.replace("\\ ", " ")).resolve() for word in words]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--clients", type=int, default=3)
    parser.add_argument("--measure", action="store_true")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    if args.clients < 3:
        parser.error("At least three clients exercise every module")
    if not args.measure and args.clients != 3:
        parser.error("Smoke mode uses three clients; larger runs need --measure")
    output = args.build_dir.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("Generated artifacts must stay outside the checkout")
    if output.exists() and any(output.iterdir()):
        parser.error("Use a fresh output directory; previous evidence is retained")
    output.mkdir(parents=True, exist_ok=True)
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    if not args.prepare_only and not compiler.is_file():
        parser.error("Compiler executable not found")
    environment = os.environ.copy()
    environment["PATH"] = str(compiler.parent) + os.pathsep + environment.get("PATH", "")
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    suffix = ".exe" if os.name == "nt" else ""
    report = {
        "compiler": str(compiler),
        "compiler_sha256": sha256(compiler) if compiler.is_file() else None,
        "source_head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
        ).strip(),
        "source_sha256": {
            str(path.relative_to(ROOT)): sha256(path)
            for path in sorted([
                Path(__file__).resolve(), *FIXTURE.glob("*"),
                *(ROOT / name for name in SUPPORT),
            ])
            if path.is_file()
        },
        "host": {"system": platform.platform(), "machine": platform.machine(),
                 "processor": platform.processor(), "logical_processors": os.cpu_count()},
        "clients": args.clients,
        "modules": 3,
        "endpoint_families_per_module": 3,
        "measured": args.measure,
        "prepare_only": args.prepare_only,
        "modes": {},
        "completed": False,
        "scope": "Host compilation, dependency fanout and functional execution; no hardware",
    }

    def run(command, folder, label, *, stdout_path=None):
        command = list(map(str, command))
        started = time.perf_counter()
        if stdout_path:
            with stdout_path.open("wb") as stream:
                result = subprocess.run(
                    command, cwd=folder, env=environment, stdout=stream,
                    stderr=subprocess.PIPE, timeout=600
                )
            text = result.stderr.decode("utf-8", errors="replace")
        else:
            result = subprocess.run(
                command, cwd=folder, env=environment, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, encoding="utf-8", errors="replace", timeout=600
            )
            text = result.stdout
        elapsed = time.perf_counter() - started
        (folder / (label + ".log")).write_text(
            f"COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {elapsed:.6f}\n{text}",
            encoding="utf-8"
        )
        if result.returncode:
            raise RuntimeError(label + " failed\n" + text)
        return text, elapsed

    try:
        for mode, typed in (("typed-header", 1), ("runtime-boundary", 0)):
            folder = output / mode
            copied = folder / "fixture"
            copied.mkdir(parents=True)
            for source in FIXTURE.glob("*"):
                if source.is_file() and source.suffix in (".hpp", ".cpp"):
                    shutil.copyfile(source, copied / source.name)
            declarations = [
                "/* Generated client declarations; one symbol per TU. MIT. */",
                "#ifndef TELEMETRY_SCALABILITY_GENERATED_CLIENTS_HPP",
                "#define TELEMETRY_SCALABILITY_GENERATED_CLIENTS_HPP",
                "#pragma once",
                "#include <array>",
            ]
            for position in range(args.clients):
                declarations.append(f'extern "C" bool client{position}() noexcept;')
                (copied / f"Client{position}.cpp").write_text(
                    "/* Generated independent consumer TU for dependency measurements. MIT. */\n"
                    f"#define SCALABILITY_TYPED_HEADER {typed}\n"
                    f"#define SCALABILITY_CLIENT_INDEX {position}\n"
                    f"#define SCALABILITY_CLIENT_FUNCTION client{position}\n"
                    '#include "ClientBody.hpp"\n',
                    encoding="utf-8"
                )
            declarations += [
                "namespace modular {",
                f"inline constexpr std::array<bool (*)() noexcept, {args.clients}> clients{{",
                ", ".join(f"client{i}" for i in range(args.clients)) + "};",
                "}",
                "#endif",
            ]
            (copied / "Clients.hpp").write_text("\n".join(declarations) + "\n", encoding="utf-8")
            entry = report["modes"][mode] = {"prepared_sources": args.clients + 5}
            if args.prepare_only:
                continue
            flags = [
                compiler, "-std=c++20", "-O2", "-UNDEBUG", "-Wall", "-Wextra",
                "-Werror", "-pedantic-errors", "-fno-exceptions", "-fno-rtti",
                "-fdiagnostics-color=never", "-I" + str(copied), "-I" + str(ROOT / "lib"),
                "-I" + str(ROOT / "lib/boost_pfr/include"),
                "-I" + str(ROOT / "lib/magic_enum"), "-I" + str(ROOT / "examples"),
            ]
            local = [copied / name for name in (
                "ModuleA.cpp", "ModuleB.cpp", "ModuleC.cpp", "Composition.cpp", "Driver.cpp"
            )] + [copied / f"Client{i}.cpp" for i in range(args.clients)]
            sources = local + [ROOT / name for name in SUPPORT]
            objects = {source: folder / (str(i) + "-" + source.stem + ".o")
                       for i, source in enumerate(sources)}
            depfiles = {source: objects[source].with_suffix(".d") for source in sources}
            compiled_seconds = {}

            def compile_source(source, label):
                _, elapsed = run(
                    [*flags, "-MMD", "-MF", depfiles[source], "-MT", objects[source].name,
                     "-c", source, "-o", objects[source]],
                    folder, label
                )
                compiled_seconds[source.name] = elapsed
                return elapsed

            def link(label):
                image = folder / ("modular" + suffix)
                _, elapsed = run([compiler, *objects.values(), "-o", image], folder, label)
                return image, elapsed

            full_started = time.perf_counter()
            for source in sources:
                compile_source(source, "full-" + source.stem)
            image, link_seconds = link("full-link")
            full_seconds = time.perf_counter() - full_started
            text, _ = run([image], folder, "full-execute")
            expected = (
                f"Modular fixture: clients={args.clients} types=17 families=3 "
                "peers=2 allocations=0"
            )
            if text.strip() != expected:
                raise RuntimeError("Unexpected or incomplete functional fixture report")

            graph = {source: dependencies(depfiles[source]) for source in sources}
            clients = [copied / f"Client{i}.cpp" for i in range(args.clients)]
            entry["client_dependency_count"] = {
                source.name: len(graph[source]) for source in clients
            }
            entry["client_private_schema_dependencies"] = {
                source.name: [
                    path.name for path in graph[source]
                    if path.parent == copied and path.name in (
                        "Common.hpp", "ModuleA.hpp", "ModuleB.hpp", "ModuleC.hpp",
                        "Composition.hpp"
                    )
                ]
                for source in clients
            }
            if typed:
                if any("ModuleA.hpp" not in entry["client_private_schema_dependencies"][s.name]
                       for s in clients):
                    raise RuntimeError("Typed consumer did not include its composition schema")
            elif any(entry["client_private_schema_dependencies"][s.name] for s in clients):
                raise RuntimeError("Runtime consumer unexpectedly includes private schemas")

            preprocessed = folder / "Client0.ii"
            _, preprocess_seconds = run(
                [*flags, "-E", clients[0]], folder, "client-preprocess",
                stdout_path=preprocessed
            )
            entry["client_preprocessed_bytes"] = preprocessed.stat().st_size
            entry["functional_report"] = text.strip()
            entry["timing_seconds"] = (
                {"full_build": full_seconds, "link": link_seconds,
                 "client_preprocess": preprocess_seconds, "compile": dict(compiled_seconds)}
                if args.measure else None
            )

            # Only copied files are changed. Checkout contents/timestamps remain
            # untouched; real compiler dependencies determine the affected TUs.
            phases = {}
            for name, changed in (
                ("implementation-change", copied / "ModuleA.cpp"),
                ("schema-header-change", copied / "ModuleA.hpp"),
            ):
                changed.write_bytes(changed.read_bytes() + b"\n// External incremental fixture edit.\n")
                affected = [
                    source for source in sources
                    if source == changed or changed in graph[source]
                ]
                started = time.perf_counter()
                for source in affected:
                    compile_source(source, name + "-" + source.stem)
                updated, _ = link(name + "-link")
                build_seconds = time.perf_counter() - started
                checked, _ = run([updated], folder, name + "-execute")
                if checked.strip() != expected:
                    raise RuntimeError("Incremental change altered fixture behavior")
                phases[name] = {
                    "changed_external_file": changed.name,
                    "compiled_translation_units": [source.name for source in affected],
                    "client_translation_units": sum(source in clients for source in affected),
                    "build_seconds": build_seconds if args.measure else None,
                }
            entry["incremental"] = phases
            if phases["implementation-change"]["compiled_translation_units"] != ["ModuleA.cpp"]:
                raise RuntimeError("Private implementation change escaped its module")
            expected_clients = args.clients if typed else 0
            if phases["schema-header-change"]["client_translation_units"] != expected_clients:
                raise RuntimeError("Unexpected consumer rebuild fanout")

            nm = compiler.with_name("nm" + suffix)
            if not nm.is_file():
                raise RuntimeError("Compiler companion nm is required for ODR/object inspection")
            symbols, _ = run([nm, "-C", image], folder, "image-symbols")
            registry_rows = [
                line for line in symbols.splitlines()
                if "TypeRegistry<" in line and line.endswith("::descriptors_")
                and ".refptr." not in line
            ]
            if len(registry_rows) != 1:
                raise RuntimeError("Expected exactly one retained structural registry")
            entry["retained_registry_definitions"] = len(registry_rows)
            entry["image_sha256"] = sha256(image)
            inspected = []
            for source in sources:
                if source.name == "Driver.cpp":
                    continue  # The explicit allocation detector calls malloc/free.
                text, _ = run([nm, "-C", "-u", objects[source]], folder,
                              "undefined-" + source.stem)
                if re.search(r"\b(?:malloc|calloc|realloc|free|operator new|operator delete)\b", text):
                    raise RuntimeError("Unexpected allocation in fixture/library object")
                definitions, _ = run([nm, "-C", objects[source]], folder,
                                     "definitions-" + source.stem)
                if "_GLOBAL__sub_I" in definitions:
                    raise RuntimeError("Unexpected eager startup constructor")
                inspected.append(source.name)
            entry["objects_without_allocation_references"] = inspected
            print(mode + ": functional, dependency, incremental and ODR checks passed", flush=True)
        report["completed"] = True
    finally:
        (output / "summary.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8"
        )
    print(json.dumps({"completed": report["completed"], "modes": list(report["modes"]),
                      "measured": args.measure, "prepare_only": args.prepare_only}), flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
