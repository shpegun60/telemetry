#!/usr/bin/env python3
"""Serve the standalone client with an optional native fake Device.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
This desktop HTTP example owns routing/status/timeout policy. It is not part
of telemetry or its JS payload codec. It listens only on localhost.
"""
import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def create_server(device, data_dir, port=0):
    device, data_dir = Path(device).resolve(), Path(data_dir).resolve()
    data_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(device), str(data_dir)], check=True, timeout=30)

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(ROOT), **kwargs)

        def log_message(self, *_):
            pass

        def do_GET(self):
            if self.path in ('/descriptor.bin', '/values.bin'):
                payload = (data_dir / self.path[1:]).read_bytes()
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
            else:
                super().do_GET()

        def do_POST(self):
            match = re.fullmatch(r'/service/(0|[1-9][0-9]{0,9})', self.path)
            if not match or int(match[1]) > 0xffffffff:
                self.send_error(404, 'Unknown endpoint route')
                return
            length = self.headers.get('Content-Length', '')
            if not re.fullmatch(r'[0-9]{1,7}', length) or int(length) > 1048576:
                self.send_error(400, 'Invalid content length')
                return
            self.connection.settimeout(10)
            payload = self.rfile.read(int(length))
            if len(payload) != int(length):
                self.send_error(400, 'Short payload')
                return
            self.server.request_count += 1
            if self.server.delay_seconds:
                time.sleep(self.server.delay_seconds)
            with tempfile.TemporaryDirectory(prefix='request-', dir=data_dir) as temporary:
                source, destination = Path(temporary) / 'input.bin', Path(temporary) / 'output.bin'
                source.write_bytes(payload)
                result = subprocess.run([str(device), 'service', match[1], str(source), str(destination)],
                                        capture_output=True, text=True, timeout=30)
                if result.returncode:
                    self.send_error(500, 'Fake Device failed')
                    return
                dispatch, endpoint, written, calls = map(int, result.stdout.split())
                response = destination.read_bytes()
                if written != len(response):
                    self.send_error(500, 'Fake Device length mismatch')
                    return
                self.server.application_calls += calls
                try:
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/octet-stream')
                    self.send_header('Content-Length', str(written))
                    self.send_header('X-Dispatch-Status', str(dispatch))
                    self.send_header('X-Endpoint-Status', str(endpoint))
                    self.end_headers()
                    self.wfile.write(response)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass  # The caller may time out after the operation executes.

    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    server.delay_seconds = 0
    server.request_count = server.application_calls = 0
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    server = create_server(args.device, args.data_dir, args.port)
    print(f'Open http://127.0.0.1:{server.server_address[1]}/examples/structured_client/', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
