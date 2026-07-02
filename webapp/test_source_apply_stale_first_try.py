#!/usr/bin/env python3
"""E2E regression for applying source once after the backend source changed."""
import copy
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

import test_visual_edge_replay as visual

ROOT = Path(__file__).parent
URL = "http://127.0.0.1:5188/"
OUT = ROOT / "test_screenshots" / "source_apply_stale_first_try"
OUT.mkdir(parents=True, exist_ok=True)


def wait_for_server():
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(URL, timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("Vite server did not become ready")


def start_vite():
    proc = subprocess.Popen(
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5188"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        wait_for_server()
    except Exception:
        proc.terminate()
        raise
    return proc


def stop_process_tree(proc):
    if proc.poll() is not None:
        return
    if sys.platform.startswith("win"):
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    state = visual.initial_state()
    source_v1 = visual.source_from_state(state)
    source_v2 = source_v1.replace("Graph VisualReplay {", "Graph VisualReplay {\n    // backend change")
    source_v3 = source_v2.replace(
        "    PrintString printer{};",
        "    // user edit after backend changed\n    PrintString printer{};",
    )
    emit_source = {"text": source_v1}
    patch_payloads = []
    source_snapshot_payloads = []
    proc = start_vite()
    results = []
    page_errors = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1400, "height": 860})
            page.on("console", lambda msg: page_errors.append(f"console:{msg.type}:{msg.text}") if msg.type in {"warning", "error"} else None)
            page.on("pageerror", lambda exc: page_errors.append(f"pageerror:{exc}"))

            page.route("**/api/state", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps(state),
            ))
            page.route("**/api/diagnostics", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({"ok": True, "stage": "session", "diagnostics": []}),
            ))
            page.route("**/api/emit", lambda route: route.fulfill(
                status=200,
                content_type="text/plain",
                body=emit_source["text"],
            ))

            def handle_source_patch(route):
                payload = json.loads(route.request.post_data or "{}")
                patch_payloads.append(payload)
                emit_source["text"] = payload.get("fallback_source", source_v3)
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "fallback": False, "state": copy.deepcopy(state)}),
                )

            def handle_source(route):
                source_snapshot_payloads.append(route.request.post_data or "")
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "state": copy.deepcopy(state)}),
                )

            page.route("**/api/source_patch", handle_source_patch)
            page.route("**/api/source", handle_source)

            page.goto(URL, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_selector('[data-canvas-context-root="true"]', state="visible", timeout=10000)
            page.get_by_role("tab", name="SOURCE").click()
            page.wait_for_function("() => Boolean(window.__graphScriptSourceEditor)", timeout=10000)

            emit_source["text"] = source_v2
            page.evaluate(
                "source => window.__graphScriptSourceEditor.setValue(source)",
                source_v3,
            )
            with page.expect_response("**/api/source_patch", timeout=10000):
                page.get_by_role("button", name="Apply source").click()
            page.screenshot(path=OUT / "stale_first_apply.png", full_page=True)

            results = [
                ("first apply used source patch", len(patch_payloads) == 1),
                ("first apply did not require full snapshot endpoint", source_snapshot_payloads == []),
                ("patch was based on latest backend source", patch_payloads and patch_payloads[0].get("base_hash") is not None),
                ("fallback source carried user buffer", patch_payloads and patch_payloads[0].get("fallback_source") == source_v3),
                ("no apply-again stale prompt", "Apply again" not in page.locator("body").inner_text(timeout=3000)),
            ]
            browser.close()
    finally:
        stop_process_tree(proc)

    failed = [name for name, ok in results if not ok]
    print("\nSource stale first-apply regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if page_errors:
        print("Page errors:")
        for error in page_errors:
            print(f"  {error}")
    if failed:
        print("Patch payloads:")
        for payload in patch_payloads:
            print(json.dumps(payload, indent=2)[:1000])
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
