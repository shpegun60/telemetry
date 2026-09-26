#!/usr/bin/env python3
"""Export the review agents' transcripts into readable, complete Markdown.

Written for the 2026-09-25 library review (see ../Review-2026-09-25.md). Claude
Code keeps each subagent's full transcript as JSONL next to the session log:

    ~/.claude/projects/<project>/<session-id>/subagents/agent-*.jsonl (+ .meta.json)

This script turns every transcript into one Markdown file that keeps everything
the agent saw and did, in order: its brief, every message it received later, its
reasoning, its text, every tool call with the full input, every tool result with
the full output, the harness attachments, timestamps and token usage. Nothing is
shortened. It also writes briefs.md (every brief and later message, verbatim) and
reports.md (every final report, verbatim), and copies the raw JSONL and metadata.

Usage:
    python export_transcripts.py --source <.../subagents> [--out <dir>]

--out defaults to this script's own directory. Only a short summary is printed.
Re-running overwrites the previous export, so run it again after an agent that
was still working has finished.
"""
import argparse
import collections
import datetime as dt
import json
import pathlib
import re
import shutil

# Agent description (from .meta.json) -> (file slug, readable role). The order of
# this table is the order of the combined files.
ROLES = [
    ("Paranoid review: numeric core", "01-numeric-core", "Reviewer: numeric core"),
    ("Paranoid review: fields", "02-fields", "Reviewer: fields"),
    ("Paranoid review: catalogs, JSON, ABI", "03-catalog-json", "Reviewer: catalogs, packed IDs, JSON, ABI"),
    ("Paranoid review: commands", "04-commands", "Reviewer: commands"),
    ("Paranoid review: slots and delegate", "05-slots", "Reviewer: slots and delegate"),
    ("Paranoid review: resource library", "06-resource", "Reviewer: resource library, protocol, adapters, web decoder"),
    ("End-user critic and market comparison", "07-critic", "Critic: end-user trial and market comparison"),
    ("Research ThingSet, CANopenNode, MAVLink", "07a-critic-helper-thingset-canopen-mavlink",
     "Critic's helper: ThingSet, CANopenNode, MAVLink"),
    ("Research Cyphal, FreeMASTER, LwM2M", "07b-critic-helper-cyphal-freemaster-lwm2m",
     "Critic's helper: Cyphal, FreeMASTER, LwM2M"),
    ("Research Zephyr MCUmgr, Pigweed, nanopb", "07c-critic-helper-zephyr-pigweed-nanopb",
     "Critic's helper: Zephyr MCUmgr, Pigweed, nanopb"),
]


def fence(text, info="text"):
    """A code fence longer than any backtick run inside the text."""
    longest = max((len(m) for m in re.findall(r"`+", text)), default=0)
    ticks = "`" * max(3, longest + 1)
    return f"{ticks}{info}\n{text}\n{ticks}"


def details(summary, body):
    return f"<details>\n<summary>{summary}</summary>\n\n{body}\n\n</details>\n"


def parse_time(value):
    try:
        return dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (AttributeError, ValueError):
        return None


def result_text(content):
    """Text of a tool_result, which is either a string or a list of blocks."""
    if isinstance(content, str):
        return content
    parts = []
    for block in content or []:
        kind = block.get("type")
        if kind == "text":
            parts.append(block.get("text", ""))
        elif kind == "image":
            parts.append("[image block: not representable as text]")
        else:
            parts.append(json.dumps(block, ensure_ascii=False, indent=2))
    return "\n".join(parts)


def handback_text(block):
    """The report text of a hand-back tool call, if this block is one."""
    if block.get("type") != "tool_use" or "handback" not in block.get("name", "").lower():
        return None
    data = block.get("input") or {}
    for key in ("report", "text", "message", "content", "result"):
        if isinstance(data.get(key), str):
            return data[key]
    return json.dumps(data, ensure_ascii=False, indent=2)


def load(path):
    records = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if line.strip():
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as error:
                records.append({"type": "unparsed", "line": number, "error": str(error), "raw": line})
    return records


def export_agent(records, meta, role, parent_role):
    """Return (markdown, brief, later_messages, final_report, stats)."""
    times = [t for t in (parse_time(r.get("timestamp")) for r in records) if t]
    tools = collections.Counter()
    errors = 0
    usage = collections.Counter()
    brief = None
    later = []
    final_report = None
    last_text = None
    names = {}
    body = []
    call_number = 0

    for record in records:
        kind = record.get("type")
        stamp = record.get("timestamp", "")
        message = record.get("message") or {}
        content = message.get("content")
        if kind == "user" and isinstance(content, str):
            if brief is None:
                brief = content
                body.append(f"### {stamp} — brief received\n\n{fence(content)}\n")
            else:
                later.append((stamp, content))
                body.append(f"### {stamp} — message received while working\n\n{fence(content)}\n")
        elif kind == "user":
            for block in content or []:
                if block.get("type") == "tool_result":
                    name = names.get(block.get("tool_use_id"), "?")
                    text = result_text(block.get("content"))
                    flag = " — **error**" if block.get("is_error") else ""
                    errors += bool(block.get("is_error"))
                    body.append(details(f"{stamp} — result of {name}{flag} ({len(text):,} chars)", fence(text)))
                elif block.get("type") == "text":
                    body.append(f"### {stamp} — user text block\n\n{fence(block.get('text', ''))}\n")
        elif kind == "assistant":
            for key, value in (message.get("usage") or {}).items():
                if isinstance(value, int):
                    usage[key] += value
            for block in content or []:
                btype = block.get("type")
                if btype == "thinking":
                    body.append(details(f"{stamp} — reasoning", block.get("thinking", "")))
                elif btype == "redacted_thinking":
                    body.append(f"*{stamp} — redacted reasoning block (not readable)*\n")
                elif btype == "text":
                    last_text = block.get("text", "")
                    body.append(f"### {stamp} — agent text\n\n{last_text}\n")
                elif btype == "tool_use":
                    call_number += 1
                    names[block.get("id")] = f"call {call_number} ({block.get('name')})"
                    tools[block.get("name")] += 1
                    report = handback_text(block)
                    if report is not None:
                        final_report = report
                    payload = json.dumps(block.get("input"), ensure_ascii=False, indent=2)
                    body.append(f"### {stamp} — tool call {call_number}: `{block.get('name')}`\n\n{fence(payload, 'json')}\n")
                else:
                    body.append(details(f"{stamp} — assistant block of type {btype}", fence(json.dumps(block, ensure_ascii=False, indent=2), "json")))
        elif kind == "attachment":
            attachment = record.get("attachment")
            label = attachment.get("type", "attachment") if isinstance(attachment, dict) else "attachment"
            rendered = record.get("rendered")
            text = rendered if isinstance(rendered, str) else json.dumps(attachment, ensure_ascii=False, indent=2)
            body.append(details(f"{stamp} — harness attachment: {label}", fence(text)))
        else:
            body.append(details(f"{stamp} — record of type {kind}", fence(json.dumps(record, ensure_ascii=False, indent=2), "json")))

    if final_report is None:
        final_report = last_text or "(no final report found in the transcript)"
    start, end = (min(times), max(times)) if times else (None, None)
    stats = {
        "start": start.isoformat() if start else "?",
        "end": end.isoformat() if end else "?",
        "minutes": round((end - start).total_seconds() / 60, 1) if times else None,
        "records": len(records),
        "tool_calls": sum(tools.values()),
        "tools": dict(tools.most_common()),
        "tool_errors": errors,
        "usage": dict(usage),
        "later_messages": len(later),
    }
    header = [
        f"# {role}",
        "",
        f"- Task description: {meta.get('description', '?')}",
        f"- Agent type: {meta.get('agentType', '?')}; spawn depth {meta.get('spawnDepth', '?')}"
        + (f"; launched by: {parent_role}" if parent_role else "; launched by: the coordinator"),
        f"- Started {stats['start']}, last record {stats['end']}, span {stats['minutes']} min",
        f"- Records {stats['records']}; tool calls {stats['tool_calls']}; tool results marked as errors {errors}",
        f"- Tool calls by tool: {', '.join(f'{k} {v}' for k, v in tools.most_common()) or 'none'}",
        f"- Token usage summed over turns (each turn re-counts its context): "
        + (", ".join(f"{k} {v:,}" for k, v in sorted(usage.items())) or "not recorded"),
        "",
        "Sections: the brief, the final report, then the complete transcript in order.",
        "Reasoning, tool results and harness attachments are folded; open them to read.",
        "",
        "## Brief (verbatim)",
        "",
        fence(brief or "(no brief found)"),
        "",
    ]
    for stamp, text in later:
        header += [f"## Message received while working, {stamp} (verbatim)", "", fence(text), ""]
    header += ["## Final report (verbatim)", "", final_report, "", "## Complete transcript", ""]
    return "\n".join(header + body), brief, later, final_report, stats


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=pathlib.Path, required=True, help="the session's subagents directory")
    parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent)
    args = parser.parse_args()

    metas = {}
    for meta_path in sorted(args.source.glob("agent-*.meta.json")):
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        metas[meta_path.name[: -len(".meta.json")]] = meta
    by_description = {meta.get("description"): stem for stem, meta in metas.items()}
    agent_ids = {stem: stem[len("agent-"):] for stem in metas}
    role_of_id = {}
    for description, slug, role in ROLES:
        stem = by_description.get(description)
        if stem:
            role_of_id[agent_ids[stem]] = role

    transcripts = args.out / "transcripts"
    raw = transcripts / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    briefs = ["# Briefs and later messages, verbatim", "",
              "Every brief an agent received and every message sent to it while it worked, "
              "exactly as delivered. Generated by export_transcripts.py.", ""]
    reports = ["# Final reports, verbatim", "",
               "Each agent's final report exactly as it handed it back. Links inside were written "
               "for the chat, relative to the workspace root, and may not resolve from this file. "
               "Generated by export_transcripts.py.", ""]
    index = []
    for description, slug, role in ROLES:
        stem = by_description.get(description)
        if not stem:
            print(f"missing transcript: {description}")
            continue
        meta = metas[stem]
        parent_role = role_of_id.get(meta.get("parentAgentId")) if meta.get("parentAgentId") else None
        records = load(args.source / f"{stem}.jsonl")
        markdown, brief, later, final_report, stats = export_agent(records, meta, role, parent_role)
        (transcripts / f"{slug}.md").write_text(markdown, encoding="utf-8")
        shutil.copyfile(args.source / f"{stem}.jsonl", raw / f"{slug}.jsonl")
        shutil.copyfile(args.source / f"{stem}.meta.json", raw / f"{slug}.meta.json")
        briefs += [f"## {role}", "", fence(brief or "(no brief found)"), ""]
        for stamp, text in later:
            briefs += [f"### Message received while working, {stamp}", "", fence(text), ""]
        reports += [f"## {role}", "", final_report, ""]
        index.append((slug, role, stats))
        print(f"{slug:45} records {stats['records']:4}  tool calls {stats['tool_calls']:4}  "
              f"span {stats['minutes']} min  later messages {stats['later_messages']}  "
              f"markdown {len(markdown):,} chars")
    (args.out / "briefs.md").write_text("\n".join(briefs), encoding="utf-8")
    (args.out / "reports.md").write_text("\n".join(reports), encoding="utf-8")
    (args.out / "transcripts-index.json").write_text(
        json.dumps([{"file": f"transcripts/{slug}.md", "role": role, **stats} for slug, role, stats in index],
                   ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
