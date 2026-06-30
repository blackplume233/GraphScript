#!/usr/bin/env python3
"""Smoke test for current asset source preview and diagnostic range focus."""
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
URL = "http://127.0.0.1:5184/"
OUT = ROOT / "test_screenshots" / "asset_source_preview_smoke"
OUT.mkdir(parents=True, exist_ok=True)

SOURCE = "\n".join([
    "graph SourceDiag {",
    "    @graph.input",
    "    param tag: GameplayTag;",
    "    node branch {",
    "        type BranchOnTag;",
    "    }",
    "    node montage {",
    "        type PlayMontage;",
    "    }",
    "    event BeginPlay {",
    "        connect(branch.matched, montage.play);",
    "        bind(tag, branch.tag);",
    "    }",
    "}",
])


def empty_target():
    return {
        "graph": "",
        "block_kind": "",
        "block_name": "",
        "node_instance": "",
        "pin_name": "",
        "parameter_name": "",
        "reference": "",
        "connection_kind": "",
    }


DIAGNOSTIC = {
    "severity": "warning",
    "message": "Preview range smoke",
    "context": "branch.matched",
    "code": "GS_ASSET_SOURCE_PREVIEW",
    "range": {
        "start": {"line": 11, "column": 17},
        "end": {"line": 11, "column": 31},
    },
    "hint": "Focus the connect source endpoint.",
    "target": empty_target(),
    "actions": [],
}

STATE = {
    "file_path": "source_diag.gs",
    "dirty": False,
    "active_graph": 0,
    "can_undo": False,
    "can_redo": False,
    "module": {
        "imports": [],
        "lets": [],
        "graphs": [{
            "name": "SourceDiag",
            "base_type": None,
            "annotations": [],
            "parameters": [{
                "name": "tag",
                "type": "GameplayTag",
                "direction": "in",
                "default": "",
                "annotations": [],
            }],
            "nodes": [
                {"type": "BranchOnTag", "instance": "branch", "init": "", "annotations": []},
                {"type": "PlayMontage", "instance": "montage", "init": "", "annotations": []},
            ],
            "events": [{
                "name": "BeginPlay",
                "kind": "event",
                "annotations": [],
                "flows": [{
                    "from_node": "branch",
                    "from_pin": "matched",
                    "to_node": "montage",
                    "to_pin": "play",
                    "annotations": [],
                    "source_range": {
                        "start": {"line": 11, "column": 9},
                        "end": {"line": 11, "column": 53},
                    },
                    "from_endpoint_source_range": {
                        "start": {"line": 11, "column": 17},
                        "end": {"line": 11, "column": 31},
                    },
                    "to_endpoint_source_range": {
                        "start": {"line": 11, "column": 33},
                        "end": {"line": 11, "column": 45},
                    },
                }],
                "links": [{
                    "target_node": "branch",
                    "target_pin": "tag",
                    "source_node": "tag",
                    "source_pin": "",
                    "annotations": [],
                }],
            }],
            "functions": [],
        }],
    },
    "types": [
        {
            "type_name": "BranchOnTag",
            "is_native": True,
            "source_graph": "",
            "tags": [],
            "annotations": [],
            "pins": [
                {"name": "enter", "kind": "exec", "direction": "in", "type": "", "annotations": []},
                {"name": "matched", "kind": "exec", "direction": "out", "type": "", "annotations": []},
                {"name": "tag", "kind": "data", "direction": "in", "type": "GameplayTag", "annotations": []},
            ],
        },
        {
            "type_name": "PlayMontage",
            "is_native": True,
            "source_graph": "",
            "tags": [],
            "annotations": [],
            "pins": [
                {"name": "play", "kind": "exec", "direction": "in", "type": "", "annotations": []},
            ],
        },
    ],
    "schemas": [],
    "diagnostics": [],
    "command_log": [],
}


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
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5184"],
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


def main():
    proc = start_vite()
    results = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1500, "height": 950})
            page.route("**/api/state", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps(STATE),
            ))
            page.route("**/api/emit", lambda route: route.fulfill(
                status=200,
                content_type="text/plain",
                body=SOURCE,
            ))
            page.route("**/api/diagnostics", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({"ok": False, "stage": "asset", "diagnostics": [DIAGNOSTIC]}),
            ))

            page.goto(URL, wait_until="networkidle", timeout=20000)
            page.wait_for_selector('[data-blueprint-node="branch"]', timeout=10000)
            page.wait_for_selector('[data-blueprint-node="montage"]', timeout=10000)
            page.wait_for_selector('[data-current-graph-source="true"]', timeout=10000)
            current_graph_source = page.locator('[data-current-graph-source="true"]').input_value()

            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector("text=connect(branch.matched, montage.play);", timeout=10000)
            page.get_by_title("Refresh source diagnostics").click()
            page.wait_for_selector("text=GS_ASSET_SOURCE_PREVIEW", timeout=10000)
            page.locator('[data-diagnostic-locate="GS_ASSET_SOURCE_PREVIEW"]').click()
            page.wait_for_selector('[data-source-line="11"][data-source-focused="true"]', timeout=5000)
            page.wait_for_selector('[data-source-range="active"]', timeout=5000)
            page.screenshot(path=OUT / "asset_source_preview.png", full_page=True)

            results = [
                ("node branch rendered", page.locator('[data-blueprint-node="branch"]').count() == 1),
                ("node montage rendered", page.locator('[data-blueprint-node="montage"]').count() == 1),
                ("current graph uses asset graph", "graph SourceDiag {" in current_graph_source),
                ("current graph uses asset node", "node branch {" in current_graph_source),
                ("current graph uses connect", "connect(branch.matched, montage.play);" in current_graph_source),
                ("source preview loaded canonical source", page.locator("text=bind(tag, branch.tag);").count() > 0),
                ("diagnostic code visible", page.locator("text=GS_ASSET_SOURCE_PREVIEW").count() > 0),
                ("source range focused", page.locator('[data-source-line="11"][data-source-focused="true"]').count() == 1),
                ("active range text", page.locator('[data-source-range="active"]').inner_text() == "branch.matched"),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nAsset source preview smoke")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
