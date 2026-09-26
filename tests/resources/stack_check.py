"""Bound individual ARM frames using measured GCC 13/14 baselines (MIT).

Authors: Ruslan Kovtun (shpegun60), codexAi.
This is not the maximum stack along nested visitors, owner callbacks or IRQs.
"""
import re

FRAME_LIMIT = 192
# Binary v2.1: CommandsFile::read is 176/152 bytes (O2/Os) on the
# reviewed GCC 13/14 builds; the v2.0 stage used 168/144. Pin the current command
# read per optimization, independently of the broader helper-frame ceiling.
# Visitor-helper frames are separate from the READ entry's own stack frame.
READ_LIMITS = {"SchemaFile": 168, "CommandsFile": 176, "ValuesFile": 152}
COMMAND_READ_LIMITS = {"O2": 176, "Os": 152}
READ_FUNCTION = re.compile(r"resource::ReadResult telemetry_resource::(SchemaFile|CommandsFile|ValuesFile)::read\(resource::Cursor, resource::Output\) const$")


def check_usage(text, required=None, optimization=None):
    frames = []
    found = set()
    for line in text.splitlines():
        if not line.strip():
            continue
        parts = line.rsplit("\t", 2)
        if len(parts) != 3 or not parts[0] or not parts[1].isdigit():
            raise RuntimeError("Malformed stack-usage entry: " + line)
        name, amount, kind = parts
        if kind not in ("static", "dynamic,bounded"):
            raise RuntimeError("Unbounded or unaudited stack usage: " + line)
        match = READ_FUNCTION.search(name)
        limit = READ_LIMITS[match[1]] if match else FRAME_LIMIT
        if match and match[1] == "CommandsFile" and optimization in COMMAND_READ_LIMITS:
            limit = COMMAND_READ_LIMITS[optimization]
        if match:
            found.add(match[1])
        size = int(amount)
        if size > limit:
            raise RuntimeError(f"Stack frame exceeds {limit} bytes: {line}")
        frames.append({"function": name, "bytes": size, "kind": kind, "limit": limit})
    if not frames or (required and required not in found):
        raise RuntimeError("Missing stack-usage report or required read function")
    return {"maximum": max(f["bytes"] for f in frames), "frames": frames}


def self_test():
    helper = "probe.cpp:1:1:bool binaryPayload()"
    read = "probe.cpp:2:1:resource::ReadResult telemetry_resource::SchemaFile::read(resource::Cursor, resource::Output) const"
    command = "probe.cpp:3:1:resource::ReadResult telemetry_resource::CommandsFile::read(resource::Cursor, resource::Output) const"
    good = f"{helper}\t192\tstatic\n{read}\t168\tdynamic,bounded\n"
    if check_usage(good, "SchemaFile")["maximum"] != 192:
        raise RuntimeError("Stack guard positive control failed")
    for opt, amount in COMMAND_READ_LIMITS.items():
        check_usage(f"{command}\t{amount}\tstatic", "CommandsFile", opt)
        for increase in (1, 8, 16):
            try:
                check_usage(f"{command}\t{amount + increase}\tstatic", "CommandsFile", opt)
            except RuntimeError:
                continue
            raise RuntimeError(f"Stack guard accepted a {increase}-byte {opt} READ regression")
    bad = ["", "not a report", f"{helper}\t-1\tstatic",
           f"{helper}\t193\tstatic", f"{helper}\t64\tdynamic",
           f"{helper}\t64\tstatic,ignoring_inline_asm", f"{read}\t169\tstatic",
           f"{command}\t177\tstatic"]
    for text in bad:
        try:
            check_usage(text)
        except RuntimeError:
            continue
        raise RuntimeError("Stack guard accepted a negative control: " + text)
    try:
        check_usage(good, "ValuesFile")
    except RuntimeError:
        return
    raise RuntimeError("Stack guard accepted a missing required frame")


if __name__ == "__main__":
    self_test()
    print("Resource stack guard: controls passed")
