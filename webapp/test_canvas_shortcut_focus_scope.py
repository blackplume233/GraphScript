#!/usr/bin/env python3
"""E2E regression for graph shortcuts only firing while the canvas owns focus."""
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
URL = "http://127.0.0.1:5187/"
OUT = ROOT / "test_screenshots" / "canvas_shortcut_focus_scope"
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
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5187"],
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
    state["can_undo"] = True
    state["can_redo"] = True
    undo_calls = []
    redo_calls = []
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
                body=visual.source_from_state(state),
            ))
            page.route("**/api/undo", lambda route: (
                undo_calls.append("undo"),
                route.fulfill(status=200, content_type="application/json", body=json.dumps({"ok": True, "state": copy.deepcopy(state)})),
            ))
            page.route("**/api/redo", lambda route: (
                redo_calls.append("redo"),
                route.fulfill(status=200, content_type="application/json", body=json.dumps({"ok": True, "state": copy.deepcopy(state)})),
            ))

            page.goto(URL, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_selector('[data-canvas-context-root="true"]', state="visible", timeout=10000)

            page.locator(".react-flow__pane").click(position={"x": 40, "y": 40})
            page.keyboard.press("Control+Z")
            page.wait_for_timeout(250)
            graph_undo_count = len(undo_calls)

            page.get_by_placeholder("Search graph").fill("branch")
            page.keyboard.press("Control+Z")
            page.wait_for_timeout(250)
            search_undo_count = len(undo_calls)

            page.locator(".react-flow__pane").click(position={"x": 40, "y": 40})
            page.keyboard.press("Control+Y")
            page.wait_for_timeout(250)
            graph_redo_count = len(redo_calls)

            page.screenshot(path=OUT / "shortcut_scope.png", full_page=True)
            results = [
                ("canvas ctrl-z invokes graph undo", graph_undo_count == 1),
                ("search input ctrl-z does not invoke graph undo", search_undo_count == graph_undo_count),
                ("canvas ctrl-y invokes graph redo", graph_redo_count == 1),
            ]
            browser.close()
    finally:
        stop_process_tree(proc)

    failed = [name for name, ok in results if not ok]
    print("\nCanvas shortcut focus-scope regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if page_errors:
        print("Page errors:")
        for error in page_errors:
            print(f"  {error}")
    if failed:
        print(f"Undo calls: {len(undo_calls)}; redo calls: {len(redo_calls)}")
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
