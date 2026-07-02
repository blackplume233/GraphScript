#!/usr/bin/env python3
"""Regression for resolving relative source imports from the session root."""
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

ROOT = Path(__file__).parent
REPO = ROOT.parent
URL = "http://127.0.0.1:5187/"


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
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5187", "--strictPort"],
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


def normalized(path):
    return str(path).replace("\\", "/").rstrip("/")


def unsaved_import_state():
    return {
        "file_path": "",
        "dirty": True,
        "active_graph": 0,
        "can_undo": False,
        "can_redo": False,
        "module": {
            "imports": [{
                "path": "tests\\fixtures\\mixed_declarations.d.gs",
                "is_native": True,
                "loaded": True,
                "normalized_path": str(REPO / "tests" / "fixtures" / "mixed_declarations.d.gs"),
                "annotations": [],
            }],
            "lets": [],
            "graphs": [{
                "name": "ImportBaseDir",
                "base_type": None,
                "annotations": [],
                "parameters": [],
                "nodes": [],
                "events": [{"name": "OnStart", "kind": "event", "annotations": [], "flows": [], "links": []}],
                "functions": [],
            }],
        },
        "declared_types": [],
        "types": [],
        "schemas": [],
        "diagnostics": [],
        "command_log": [],
    }


def saved_preset_import_state():
    return {
        "file_path": "tests\\fixtures\\minimal.gs",
        "dirty": False,
        "active_graph": 0,
        "can_undo": False,
        "can_redo": False,
        "module": {
            "imports": [{
                "path": "ue_core.d.gs",
                "is_native": False,
                "loaded": False,
                "normalized_path": str(REPO / "tests" / "fixtures" / "ue_core.d.gs"),
                "annotations": [],
            }],
            "lets": [],
            "graphs": [{
                "name": "HelloWorld",
                "base_type": None,
                "annotations": [],
                "parameters": [{"name": "message", "type": "FString", "direction": "in", "default": "", "annotations": []}],
                "nodes": [{"type": "PrintString", "instance": "printer", "init": "", "annotations": []}],
                "events": [{"name": "OnStart", "kind": "event", "annotations": [], "flows": [], "links": []}],
                "functions": [],
            }],
        },
        "declared_types": [{"name": "FString", "constructible": False, "source_file": str(REPO / "presets" / "ue_core.d.gs"), "annotations": []}],
        "types": [{
            "type_name": "PrintString",
            "is_native": True,
            "source_graph": "",
            "source_file": str(REPO / "presets" / "ue_core.d.gs"),
            "tags": [],
            "annotations": [],
            "pins": [{"name": "enter", "kind": "exec", "direction": "in", "type": "Exec", "source_file": str(REPO / "presets" / "ue_core.d.gs"), "annotations": []}],
        }],
        "schemas": [],
        "diagnostics": [],
        "command_log": [],
    }


def run_case(case_name, state_payload, emit_source):
    diagnostics_requests = []
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1400, "height": 900})
        page.route("**/api/state", lambda route: route.fulfill(
            status=200,
            content_type="application/json",
            body=json.dumps(state_payload),
        ))
        page.route("**/api/emit", lambda route: route.fulfill(
            status=200,
            content_type="text/plain",
            body=emit_source,
        ))

        def handle_diagnostics(route):
            diagnostics_requests.append(json.loads(route.request.post_data or "{}"))
            route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({"ok": True, "stage": "session", "diagnostics": [], "environment": {"declarations": []}}),
            )

        page.route("**/api/diagnostics", handle_diagnostics)
        page.route("**/api/completion", lambda route: route.fulfill(
            status=200,
            content_type="application/json",
            body=json.dumps({"ok": True, "items": []}),
        ))
        page.goto(URL, wait_until="domcontentloaded", timeout=20000)
        page.wait_for_function(f"() => document.body.innerText.includes('{case_name}')", timeout=10000)
        page.get_by_role("button", name="Refresh source diagnostics").click()
        deadline = time.time() + 5
        while time.time() < deadline and not diagnostics_requests:
            page.wait_for_timeout(100)
        browser.close()

    return normalized(diagnostics_requests[-1].get("base_dir", "")) if diagnostics_requests else ""


def main():
    proc = start_vite()
    try:
        cases = [
            (
                "ImportBaseDir",
                unsaved_import_state(),
                'import "tests\\\\fixtures\\\\mixed_declarations.d.gs";\n\ngraph ImportBaseDir {\n    event OnStart {\n    }\n}\n',
                normalized(REPO),
            ),
            (
                "HelloWorld",
                saved_preset_import_state(),
                'import "ue_core.d.gs";\n\ngraph HelloWorld {\n    @graph.input\n    param message: FString;\n    node printer {\n        type PrintString;\n    }\n    event OnStart {\n    }\n}\n',
                normalized(REPO / "presets"),
            ),
        ]

        results = []
        for case_name, state_payload, emit_source, expected_base in cases:
            actual_base = run_case(case_name, state_payload, emit_source)
            results.append((case_name, expected_base, actual_base))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("\nSource import base dir regression")
    for case_name, expected_base, actual_base in results:
        print(f"  {case_name} expected: {expected_base}")
        print(f"  {case_name} actual:   {actual_base}")
        if actual_base != expected_base:
            raise SystemExit("Source diagnostics should resolve imports from the active declaration root")


if __name__ == "__main__":
    main()
