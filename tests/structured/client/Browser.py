#!/usr/bin/env python3
"""Drive the real structural editor and encoded C++ fake Device in a browser.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Requires Playwright; uses a caller-selected browser or its pinned Chromium.
"""
import argparse
import os
from pathlib import Path
import runpy
import struct
import sys
import threading
import time

from playwright.sync_api import sync_playwright

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'examples/structured_client'))
from serve import create_server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    out = args.build_dir.resolve()
    server = create_server(args.device, out)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    checks = 0

    def check(condition):
        nonlocal checks
        assert condition
        checks += 1

    try:
        with sync_playwright() as playwright:
            launch = dict(headless=True)
            if os.environ.get('TELEMETRY_BROWSER_EXECUTABLE'):
                launch['executable_path'] = os.environ['TELEMETRY_BROWSER_EXECUTABLE']
            browser = playwright.chromium.launch(**launch)
            page = browser.new_page(viewport={'width': 1180, 'height': 900})
            errors = []
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.goto(f'http://127.0.0.1:{server.server_address[1]}/examples/structured_client/')
            page.locator('#load-device').click()
            page.wait_for_function("document.querySelector('#values').textContent.includes('18446744073709551615')")
            check('3 services' in page.locator('#model-info').inner_text())
            check(page.locator('#editor input[min], #editor input[max], #editor input[step]').count() == 0)
            for member, value in {
                'serial': '18446744073709551615', 'offset': '-9223372036854775808',
                'gain': '-0', 'precise': '0.125', 'mode': '-2', 'multiplier': '4',
                '0': '1', '1': '256', '2': '65535',
            }.items():
                page.locator(f'input[data-member="{member}"]').fill(value)
            page.locator('input[data-member="enabled"]').check()
            with page.expect_download() as download:
                page.locator('#download').click()
            check(Path(download.value.path()).read_bytes() == (out / 'request.bin').read_bytes())
            page.locator('#call').click()
            page.wait_for_function("document.querySelector('#message').textContent.startsWith('Ok')")
            check('263168' in page.locator('#response').inner_text())
            check('18446744073709551615' in page.locator('#response').inner_text())
            check(server.request_count == 1 and server.application_calls == 1)
            page.screenshot(path=str(out / 'client-desktop.png'), full_page=True)

            for value, status in [('0', 'InvalidArgument'), ('1', 'Busy'), ('2', 'Unavailable'), ('3', 'Failed')]:
                page.locator('input[data-member="multiplier"]').fill(value)
                page.locator('#call').click()
                page.wait_for_function('(status) => document.querySelector("#message").textContent === status', arg=status)
                check(page.locator('#message').inner_text() == status)
            page.locator('#service').select_option('1')
            check(page.locator('#editor input').count() == 0)
            page.locator('#call').click()
            page.wait_for_function("document.querySelector('#response').textContent.includes('Void')")
            check(page.locator('#message').inner_text().startswith('Ok'))
            page.locator('#service').select_option('2')
            page.locator('#call').click()
            page.wait_for_function("document.querySelector('#message').textContent === 'Unavailable'")
            check(server.request_count == 7 and server.application_calls == 6)

            # A delayed operation can still execute after a client timeout.
            # Only one request is sent, and no automatic retry is performed.
            page.locator('#service').select_option('0')
            page.locator('input[data-member="multiplier"]').fill('4')
            server.delay_seconds = 5.3
            page.locator('#call').click()
            page.wait_for_function("document.querySelector('#message').textContent.startsWith('Timeout')", timeout=10000)
            deadline = time.monotonic() + 5
            while server.application_calls < 7 and time.monotonic() < deadline:
                time.sleep(0.05)
            check(server.request_count == 8 and server.application_calls == 7)
            check('not repeated' in page.locator('#message').inner_text())
            server.delay_seconds = 0

            wrong_values = bytearray((out / 'values.bin').read_bytes())
            wrong_values[16] ^= 1
            (out / 'wrong-values.bin').write_bytes(wrong_values)
            page.locator('#values-file').set_input_files(str(out / 'wrong-values.bin'))
            page.wait_for_function("document.querySelector('#message').textContent.includes('fingerprint')")
            check('fingerprint' in page.locator('#message').inner_text())

            # A reflected name is literal text, never HTML or an expression.
            oracle = runpy.run_path(str(HERE.parent / 'descriptor/parser.py'))
            descriptor = bytearray((out / 'descriptor.bin').read_bytes())
            model = oracle['parse'](descriptor)
            request_type = model['types'][model['endpoints'][2][0]['type']]
            position = request_type['offset'] + 24
            position += 8 + len(request_type['members'][0][1].encode('utf-8'))
            name = b'<b>hi</b> '
            check(len(name) == len(request_type['members'][1][1]))
            descriptor[position + 8:position + 8 + len(name)] = name
            struct.pack_into('<Q', descriptor, 16, oracle['fingerprint'](descriptor))
            oracle['parse'](descriptor)
            (out / 'literal-name.bin').write_bytes(descriptor)
            page.locator('#descriptor-file').set_input_files(str(out / 'literal-name.bin'))
            page.wait_for_function("document.querySelector('#editor').textContent.includes('<b>hi</b>')")
            check(page.locator('#editor b').count() == 0)
            check(page.locator('#call').is_disabled())
            check(not errors)
            browser.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
    print(f'Structured browser: {checks} checks, 0 failures', flush=True)


if __name__ == '__main__':
    main()
