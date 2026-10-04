#!/usr/bin/env python3
"""Exercise Qt Creator's AST request sequence against an independent clangd."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import time


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_messages(stream, messages):
    try:
        while True:
            headers = {}
            while True:
                line = stream.readline()
                if not line:
                    messages.put(None)
                    return
                if line in (b"\r\n", b"\n"):
                    break
                key, value = line.decode("ascii").split(":", 1)
                headers[key.lower()] = value.strip()
            length = int(headers["content-length"])
            body = stream.read(length)
            if len(body) != length:
                raise RuntimeError("Incomplete LSP response")
            messages.put(json.loads(body))
    except Exception as error:
        messages.put(error)


def tree_nodes(value):
    count = 0
    pending = [value]
    while pending:
        current = pending.pop()
        if isinstance(current, dict):
            count += 1
            pending.extend(current.values())
        elif isinstance(current, list):
            pending.extend(current)
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clangd", required=True, type=Path)
    parser.add_argument("--compile-commands-dir", required=True, type=Path)
    parser.add_argument("--file", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=25)
    parser.add_argument("--profile", choices=("reproduction", "qtcreator"),
                        default="reproduction")
    parser.add_argument("--navigation-check", action="store_true")
    parser.add_argument("--clangd-arg", action="append", default=[])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (args.file or root / "lib/telemetry/model/Model.hpp").resolve()
    executable = args.clangd.resolve()
    database_dir = args.compile_commands_dir.resolve()
    database = database_dir / "compile_commands.json"
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    launch_options = (["--background-index=0", "--log=verbose", "-j=1"]
                      if args.profile == "reproduction" else [
                          "--background-index", "--header-insertion=never",
                          "--limit-results=100", "--limit-references=0", "--clang-tidy=0",
                          "--background-index-priority=low", "--rename-file-limit=0",
                          "--use-dirty-headers"])
    command = [str(executable), "--compile-commands-dir=" + str(database_dir),
               *launch_options, *args.clangd_arg]
    summary = {
        "command": command,
        "executable_sha256": sha256(executable),
        "file": str(source),
        "file_sha256": sha256(source),
        "compile_commands_sha256": sha256(database),
        "requests": [],
        "diagnostics": {},
        "error": None,
    }
    messages = queue.Queue()
    with (output / "stderr.log").open("wb") as stderr:
        process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=stderr, cwd=root)
        reader = threading.Thread(target=read_messages,
                                  args=(process.stdout, messages), daemon=True)
        reader.start()

        def send(message):
            body = json.dumps(message, ensure_ascii=False).encode("utf-8")
            process.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body)
            process.stdin.flush()

        def request(identifier, method, params):
            started = time.monotonic()
            summary["pending_request"] = {"id": identifier, "method": method, "params": params}
            send({"jsonrpc": "2.0", "id": identifier, "method": method, "params": params})
            deadline = started + args.timeout
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("Timed out waiting for " + method)
                try:
                    message = messages.get(timeout=remaining)
                except queue.Empty:
                    raise TimeoutError("Timed out waiting for " + method) from None
                if message is None:
                    raise RuntimeError("clangd closed stdout; process exit=" + str(process.poll()))
                if isinstance(message, Exception):
                    raise message
                if message.get("method") == "textDocument/publishDiagnostics":
                    notification = message["params"]
                    summary["diagnostics"][notification["uri"]] = notification["diagnostics"]
                if "method" in message and "id" in message:
                    send({"jsonrpc": "2.0", "id": message["id"], "result": None})
                    continue
                if message.get("id") != identifier:
                    continue
                (output / f"{identifier}-reply.json").write_text(
                    json.dumps(message, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
                result = message.get("result")
                summary["requests"].append({
                    "id": identifier, "method": method, "params": params,
                    "seconds": time.monotonic() - started,
                    "error": message.get("error"),
                    "result_type": type(result).__name__,
                    "result_json_bytes": len(json.dumps(result).encode("utf-8")),
                    "result_tree_nodes": tree_nodes(result),
                })
                summary.pop("pending_request", None)
                if "error" in message:
                    raise RuntimeError("LSP request failed: " + str(message["error"]))
                return result

        uri = source.as_uri()
        try:
            initialized = request(1, "initialize", {
                "processId": os.getpid(), "rootUri": root.as_uri(), "capabilities": {}})
            summary["server_info"] = initialized.get("serverInfo")
            send({"jsonrpc": "2.0", "method": "initialized", "params": {}})
            send({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
                "textDocument": {"uri": uri, "languageId": "cpp", "version": 1,
                                 "text": source.read_text(encoding="utf-8")}}})
            request(2, "textDocument/hover", {"textDocument": {"uri": uri},
                    "position": {"line": 118, "character": 48}})
            ranged = request(3, "textDocument/ast", {"textDocument": {"uri": uri},
                    "range": {"start": {"line": 118, "character": 44},
                              "end": {"line": 118, "character": 50}}})
            whole = request(4, "textDocument/ast", {"textDocument": {"uri": uri}})
            if not isinstance(ranged, dict) or not isinstance(whole, dict):
                raise RuntimeError("Expected AST objects for both requests")
            next_id = 5
            if args.navigation_check:
                source_lines = source.read_text(encoding="utf-8").splitlines()
                line = next(index for index, text in enumerate(source_lines)
                            if "static consteval TypeId typeId" in text)
                position = {"line": line, "character": source_lines[line].index("TypeId") + 2}
                params = {"textDocument": {"uri": uri}, "position": position}
                hover = request(5, "textDocument/hover", params)
                definition = request(6, "textDocument/definition", params)
                if not hover or not definition:
                    raise RuntimeError("Expected TypeId hover and definition")
                next_id = 7
            source_diagnostics = summary["diagnostics"].get(uri)
            if source_diagnostics is None:
                raise RuntimeError("No diagnostic report received for the opened header")
            if any(item.get("severity") == 1 for item in source_diagnostics):
                raise RuntimeError("clangd reported errors for the opened header")
            request(next_id, "shutdown", None)
            send({"jsonrpc": "2.0", "method": "exit"})
            process.wait(timeout=args.timeout)
        except Exception as error:
            summary["error"] = str(error)
        finally:
            if process.poll() is None:
                try:
                    process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=5)
            process.stdin.close()
            process.stdout.close()
            reader.join(timeout=1)
            summary["exit_code"] = process.returncode
            if "pending_request" in summary:
                summary["failed_request"] = summary.pop("pending_request")
            if summary["error"] and "process exit=None" in summary["error"]:
                summary["error"] = summary["error"].replace(
                    "process exit=None", "process exit=" + str(process.returncode))
    summary["passed"] = summary["error"] is None and summary["exit_code"] == 0
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"passed": summary["passed"], "exit_code": summary["exit_code"],
                      "error": summary["error"], "requests": len(summary["requests"]),
                      "summary": str(output / "summary.json")}, indent=2))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
