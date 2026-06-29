#!/usr/bin/env python3
"""Smoke test for DiagnosticTarget UI behavior against a real gs serve backend."""
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

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
GS_EXE = REPO / "build" / "Release" / "gs.exe"
CORE_DECL = REPO / "presets" / "ue_core.d.gs"
INVALID_SOURCE = REPO / "tests" / "fixtures" / "invalid_connection.gs"
PORT = 8095
URL = f"http://127.0.0.1:{PORT}/"
OUT = ROOT / "test_screenshots" / "real_backend_diagnostic_target_smoke"
OUT.mkdir(parents=True, exist_ok=True)


def wait_for_server():
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(URL + "api/state", timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("gs serve did not become ready")


def start_gs_serve():
    if not GS_EXE.exists():
        raise RuntimeError(f"Missing executable: {GS_EXE}")
    if not CORE_DECL.exists():
        raise RuntimeError(f"Missing declaration fixture: {CORE_DECL}")
    if not INVALID_SOURCE.exists():
        raise RuntimeError(f"Missing source fixture: {INVALID_SOURCE}")

    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    proc = subprocess.Popen(
        [
            str(GS_EXE),
            "serve",
            "-p",
            str(PORT),
            "-I",
            str(CORE_DECL),
            "-i",
            str(INVALID_SOURCE),
        ],
        cwd=REPO,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        creationflags=creationflags,
    )
    try:
        wait_for_server()
    except Exception:
        proc.terminate()
        raise
    return proc


def fetch_json(path):
    with urllib.request.urlopen(URL + path, timeout=5) as res:
        return json.loads(res.read().decode("utf-8"))


def border_color(locator):
    return locator.evaluate("el => getComputedStyle(el).borderColor")


def font_weight(locator):
    return int(locator.evaluate("el => getComputedStyle(el).fontWeight"))


def main():
    proc = start_gs_serve()
    results = []
    try:
        initial_state = fetch_json("api/state")
        diagnostics_response = fetch_json("api/diagnostics")
        initial_diagnostics = initial_state.get("diagnostics", [])
        api_diagnostics = diagnostics_response.get("diagnostics", [])
        duplicate = next(
            (diag for diag in initial_diagnostics if diag.get("code") == "GS_GRAPH_DUPLICATE_CONNECTION"),
            {},
        )
        api_duplicate = next(
            (diag for diag in api_diagnostics if diag.get("code") == "GS_GRAPH_DUPLICATE_CONNECTION"),
            {},
        )
        target = duplicate.get("target", {})
        actions = duplicate.get("actions", [])
        api_actions = api_duplicate.get("actions", [])

        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1500, "height": 950})
            page.goto(URL, wait_until="networkidle", timeout=20000)
            page.wait_for_selector(".node-card:has-text('a')", timeout=10000)
            page.wait_for_selector(".node-card:has-text('b')", timeout=10000)
            page.wait_for_selector("text=GS_GRAPH_DUPLICATE_CONNECTION", timeout=10000)
            page.wait_for_selector(
                '[data-line-id="a_exec-out-exit-b_exec-in-enter"]',
                state="attached",
                timeout=10000,
            )
            page.wait_for_timeout(700)
            page.screenshot(path=OUT / "01_loaded_real_diagnostic.png", full_page=True)

            node_a = page.locator(".node-card:has-text('a')").first
            pin_exit = node_a.locator("text=exit").first
            diagnostic_code_visible = page.locator("text=GS_GRAPH_DUPLICATE_CONNECTION").count() >= 1
            initial_node_highlighted = border_color(node_a) != "rgb(0, 0, 0)"
            initial_pin_highlighted = font_weight(pin_exit) >= 600
            initial_edge_highlighted = page.locator(".gs-diagnostic-line-warning").count() >= 1

            page.locator('[data-diagnostic-locate]:has-text("a.exit")').first.click()
            page.wait_for_selector('[data-node-instance="a"]', timeout=5000)
            page.wait_for_selector(".gs-diagnostic-line-focused", timeout=5000)
            page.screenshot(path=OUT / "02_focused_real_diagnostic.png", full_page=True)

            focused_node_selected = page.locator('[data-node-instance="a"]').count() == 1
            focused_edge_highlighted = page.locator(".gs-diagnostic-line-focused").count() >= 1

            page.get_by_role("button", name="Remove duplicate connection").first.click()
            page.wait_for_timeout(700)
            fixed_state = fetch_json("api/state")
            fixed_diagnostics = fixed_state.get("diagnostics", [])
            graph = fixed_state["module"]["graphs"][0] if fixed_state["module"]["graphs"] else {}
            event = graph["events"][0] if graph.get("events") else {}
            flows = event.get("flows", [])
            command_log = fixed_state.get("command_log", [])
            page.screenshot(path=OUT / "03_after_real_quickfix.png", full_page=True)

            results = [
                ("real backend exposes duplicate diagnostic", bool(duplicate)),
                ("real backend diagnostic id exposed", str(duplicate.get("id", "")).startswith("diagnostic:GS_GRAPH_DUPLICATE_CONNECTION")),
                ("api diagnostics id exposed", str(api_duplicate.get("id", "")).startswith("diagnostic:GS_GRAPH_DUPLICATE_CONNECTION")),
                ("api diagnostic action id exposed", any(
                    str(action.get("id", "")).startswith("diagnostic-action:diagnostic:GS_GRAPH_DUPLICATE_CONNECTION")
                    for action in api_actions
                )),
                ("diagnostic target graph", target.get("graph") == "BadGraph"),
                ("diagnostic target block", target.get("block_kind") == "event" and target.get("block_name") == "OnStart"),
                ("diagnostic target node pin", target.get("node_instance") == "a" and target.get("pin_name") == "exit"),
                ("diagnostic target connection kind", target.get("connection_kind") == "exec"),
                ("diagnostic quickfix command exposed", any(
                    action.get("command") == "unflow a.exit b.enter" for action in actions
                )),
                ("diagnostic code visible in Web UI", diagnostic_code_visible),
                ("node target highlighted from real state", initial_node_highlighted),
                ("pin target highlighted from real state", initial_pin_highlighted),
                ("connection target highlighted from real state", initial_edge_highlighted),
                ("locate selects target node", focused_node_selected),
                ("locate focuses target connection", focused_edge_highlighted),
                ("quickfix records block context then command", command_log[-2:] == [
                    "event OnStart",
                    "unflow a.exit b.enter",
                ]),
                ("quickfix clears duplicate diagnostic", not any(
                    diag.get("code") == "GS_GRAPH_DUPLICATE_CONNECTION" for diag in fixed_diagnostics
                )),
                ("quickfix leaves one flow", len(flows) == 1 and all(
                    flows[0].get(key) == value for key, value in {
                        "from_node": "a",
                        "from_pin": "exit",
                        "to_node": "b",
                        "to_pin": "enter",
                    }.items()
                )),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nReal backend DiagnosticTarget smoke")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
