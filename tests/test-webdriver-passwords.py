#!/usr/bin/env python3
#
# Copyright (c) 2026, Superbird contributors.
#
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import http.server
import json
import runpy
import tempfile
import threading
import time
from pathlib import Path


def run_test(webdriver_binary: str, helpers_path: Path):
    helpers = runpy.run_path(str(helpers_path))

    login_html = b"""<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>Superbird Login Test</title></head>
<body>
  <form id="classic-form" action="/submitted" method="POST">
    <input id="user" name="username" type="text" autocomplete="username">
    <input id="pass" name="password" type="password" autocomplete="current-password">
    <button id="submit-btn" type="submit">Sign In</button>
  </form>

  <form id="spa-form">
    <input id="spa-email" type="email" autocomplete="email">
    <input id="spa-pass" type="password" autocomplete="current-password">
    <button id="spa-submit" type="submit">SPA Sign In</button>
  </form>
  <div id="spa-status"></div>

  <iframe id="cross-origin-frame" src="CROSS_ORIGIN_URL"></iframe>

  <script>
    window.__spaSubmitted = false;
    window.__events = [];
    for (const id of ['user', 'pass', 'spa-email', 'spa-pass']) {
      const el = document.getElementById(id);
      el.addEventListener('input', (e) => window.__events.push(id + ':input:' + e.isTrusted));
      el.addEventListener('change', (e) => window.__events.push(id + ':change:' + e.isTrusted));
    }
    document.getElementById('spa-form').addEventListener('submit', (e) => {
      e.preventDefault();
      window.__spaSubmitted = true;
      document.getElementById('spa-status').textContent = 'submitted:' + document.getElementById('spa-email').value;
    });
  </script>
</body>
</html>
"""

    iframe_html = b"""<!DOCTYPE html>
<html><body>
  <form><input id="x-user" name="username"><input id="x-pass" type="password"></form>
</body></html>
"""

    class Origin2Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(iframe_html)

        def log_message(self, fmt, *args):
            pass

    server2 = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Origin2Handler)
    port2 = server2.server_address[1]
    threading.Thread(target=server2.serve_forever, daemon=True).start()

    page_bytes = login_html.replace(b"CROSS_ORIGIN_URL", f"http://127.0.0.1:{port2}/iframe".encode())

    class Origin1Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(page_bytes)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            if length > 0:
                self.rfile.read(length)
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"<!DOCTYPE html><html><body><h1 id='done'>Logged In</h1></body></html>")

        def log_message(self, fmt, *args):
            pass

    server1 = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Origin1Handler)
    port1 = server1.server_address[1]
    threading.Thread(target=server1.serve_forever, daemon=True).start()

    try:
        with tempfile.TemporaryDirectory() as directory:
            process, wd_port = helpers["launch_webdriver"](
                webdriver_binary, "--headless", "--profile-path", directory
            )
            session = None
            try:
                session = helpers["create_session"](wd_port)

                def command(path, body):
                    status, payload, raw = helpers["request"](wd_port, "POST", f"/session/{session}/{path}", body)
                    assert status == 200, f"POST {path} failed: {raw}"
                    return payload["value"]

                def script(source, *args):
                    return command("execute/sync", {"script": source, "args": list(args)})

                # 1. Verify about:settings#superbird loads cleanly and Passwords enable/disable & options round-trip via Hub
                command("url", {"url": "about:settings#superbird"})
                settings_loaded = command(
                    "execute/async",
                    {
                        "script": """
                        const done = arguments[0];
                        function check() {
                            const text = document.body.innerText || '';
                            if (text.includes('Superbird') && text.includes('Modules')) {
                                return done({ ok: true, hasPasswordsCard: text.includes('Passwords') });
                            }
                            return false;
                        }
                        if (!check()) {
                            const obs = new MutationObserver(() => {
                                if (check()) obs.disconnect();
                            });
                            obs.observe(document.documentElement, { childList: true, subtree: true, characterData: true });
                            setTimeout(() => { obs.disconnect(); done({ ok: false, text: document.body.innerText }); }, 8000);
                        }
                        """,
                        "args": [],
                    },
                )
                assert settings_loaded.get("ok"), f"about:settings#superbird failed to load: {settings_loaded}"
                print(f"  [PASS] about:settings#superbird rendered cleanly (UI module cards present: {settings_loaded.get('hasPasswordsCard')})")

                # 2. Verify live module disable, re-enable, and option updates via Settings WebUI interface
                toggled_off = command(
                    "execute/async",
                    {
                        "script": """
                        const done = arguments[0];
                        document.addEventListener('WebUIMessage', function listener(ev) {
                            if (ev.detail.name !== 'loadSettings') return;
                            document.removeEventListener('WebUIMessage', listener);
                            done(ev.detail.data);
                        });
                        ladybird.sendMessage('setSuperbirdModuleEnabled', { id: 'passwords', enabled: false });
                        """,
                        "args": [],
                    },
                )
                assert toggled_off["superbird"]["modules"]["passwords"] is False, toggled_off

                toggled_on = command(
                    "execute/async",
                    {
                        "script": """
                        const done = arguments[0];
                        document.addEventListener('WebUIMessage', function listener(ev) {
                            if (ev.detail.name !== 'loadSettings') return;
                            document.removeEventListener('WebUIMessage', listener);
                            done(ev.detail.data);
                        });
                        ladybird.sendMessage('setSuperbirdModuleEnabled', { id: 'passwords', enabled: true });
                        """,
                        "args": [],
                    },
                )
                assert toggled_on["superbird"]["modules"]["passwords"] is True, toggled_on

                opt_updated = command(
                    "execute/async",
                    {
                        "script": """
                        const done = arguments[0];
                        document.addEventListener('WebUIMessage', function listener(ev) {
                            if (ev.detail.name !== 'loadSettings') return;
                            document.removeEventListener('WebUIMessage', listener);
                            done(ev.detail.data);
                        });
                        ladybird.sendMessage('setSuperbirdModuleOption', { module: 'passwords', option: 'autoLockMinutes', value: '5' });
                        """,
                        "args": [],
                    },
                )
                assert opt_updated["superbird"]["options"]["passwords"]["autoLockMinutes"] == "5", opt_updated
                print("  [PASS] Live enable/disable & option persistence for Passwords module in about:settings")

                # 3. Load multi-origin login test page (static form + SPA form + cross-origin iframe)
                command("url", {"url": f"http://127.0.0.1:{port1}/login"})
                time.sleep(0.3)

                # Focus SPA fields, type credentials, submit SPA preventDefault() form
                spa_email = command("element", {"using": "css selector", "value": "#spa-email"})
                spa_pass = command("element", {"using": "css selector", "value": "#spa-pass"})
                spa_btn = command("element", {"using": "css selector", "value": "#spa-submit"})
                el_key = next(iter(spa_email.keys()))
                command(f"element/{spa_email[el_key]}/value", {"text": "alice@example.com"})
                command(f"element/{spa_pass[el_key]}/value", {"text": "MySpaPass!23"})
                command(f"element/{spa_btn[el_key]}/click", {})
                time.sleep(0.2)
                spa_status = script("return document.getElementById('spa-status').textContent;")
                assert spa_status == "submitted:alice@example.com", spa_status
                print("  [PASS] Headless Superbird SPA login form focus/input/submit flow clean")

                # Submit classic POST form (exercises HTMLFormElement::submit_form credential observer across Hub -> UI)
                classic_user = command("element", {"using": "css selector", "value": "#user"})
                classic_pass = command("element", {"using": "css selector", "value": "#pass"})
                classic_btn = command("element", {"using": "css selector", "value": "#submit-btn"})
                command(f"element/{classic_user[el_key]}/value", {"text": "bob@example.com"})
                command(f"element/{classic_pass[el_key]}/value", {"text": "ClassicPw!45"})
                command(f"element/{classic_btn[el_key]}/click", {})
                time.sleep(0.4)
                done_heading = script("return document.getElementById('done') ? document.getElementById('done').textContent : '';")
                assert done_heading == "Logged In", done_heading
                print("  [PASS] Headless Superbird classic POST login form submit & navigation flow clean")

            finally:
                if session:
                    try:
                        helpers["request"](wd_port, "DELETE", f"/session/{session}")
                    except Exception:
                        pass
                process.terminate()
                process.wait(timeout=10)
    finally:
        server1.shutdown()
        server2.shutdown()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--webdriver", required=True)
    parser.add_argument("--helpers", required=True)
    args = parser.parse_args()
    print("Running test-webdriver-passwords...")
    run_test(args.webdriver, Path(args.helpers))
    print("All WebDriver Passwords checks passed!")


if __name__ == "__main__":
    main()
