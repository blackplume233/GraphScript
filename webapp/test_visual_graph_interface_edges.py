#!/usr/bin/env python3
"""Regression for rendering graph parameters and context pins as visual edges."""
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
URL = "http://127.0.0.1:5182/"
OUT = ROOT / "test_screenshots" / "visual_graph_interface_edges"
OUT.mkdir(parents=True, exist_ok=True)


def state():
    return {
        "file_path": "minimal.gs",
        "dirty": False,
        "active_graph": 0,
        "can_undo": False,
        "can_redo": False,
        "module": {
            "imports": [],
            "lets": [],
            "graphs": [{
                "name": "HelloWorld",
                "base_type": None,
                "annotations": [],
                "parameters": [{
                    "name": "message",
                    "type": "FString",
                    "direction": "in",
                    "default": "",
                    "annotations": [],
                }],
                "nodes": [{
                    "type": "PrintString",
                    "instance": "printer",
                    "init": "",
                    "annotations": [{
                        "name": "Position",
                        "args": [{"name": "X", "value": "260"}, {"name": "Y", "value": "120"}],
                    }],
                }],
                "events": [{
                    "name": "OnStart",
                    "kind": "event",
                    "annotations": [],
                    "flows": [{
                        "from_node": "context",
                        "from_pin": "start",
                        "to_node": "printer",
                        "to_pin": "enter",
                        "annotations": [],
                    }],
                    "links": [{
                        "source_node": "message",
                        "source_pin": "",
                        "target_node": "printer",
                        "target_pin": "message",
                        "annotations": [],
                    }],
                }],
                "functions": [],
            }],
        },
        "declared_types": [{"name": "FString", "constructible": True, "annotations": []}],
        "types": [{
            "type_name": "PrintString",
            "is_native": True,
            "source_graph": "",
            "tags": ["IO"],
            "annotations": [],
            "pins": [
                {"name": "enter", "kind": "exec", "direction": "in", "type": "Exec", "annotations": []},
                {"name": "exit", "kind": "exec", "direction": "out", "type": "Exec", "annotations": []},
                {"name": "message", "kind": "data", "direction": "in", "type": "FString", "annotations": []},
            ],
        }],
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
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5182", "--strictPort"],
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
    current = state()
    proc = start_vite()
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1450, "height": 900})
            page.route("**/api/state", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps(current),
            ))
            page.route("**/api/diagnostics", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({"ok": True, "stage": "session", "diagnostics": []}),
            ))
            page.route("**/api/emit", lambda route: route.fulfill(
                status=200,
                content_type="text/plain",
                body="graph HelloWorld {}",
            ))
            exec_commands = []
            page.route("**/api/exec", lambda route: (
                exec_commands.append(json.loads(route.request.post_data or "{}").get("command", "")),
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "state": current}),
                ),
            ))
            page.route("**/api/completion", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({"ok": True, "items": []}),
            ))
            page.goto(URL, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_selector('[data-blueprint-node="context"]', timeout=10000)
            page.wait_for_selector('[data-blueprint-node="Graph Inputs"]', timeout=10000)
            page.wait_for_selector('[data-blueprint-node="printer"]', timeout=10000)
            page.wait_for_timeout(700)

            context_before = page.locator('[data-blueprint-node="context"]').bounding_box()
            inputs_before = page.locator('[data-blueprint-node="Graph Inputs"]').bounding_box()
            if not context_before or not inputs_before:
                raise RuntimeError("Expected synthetic nodes to have visible bounds")
            inputs_hit_before = page.evaluate(
                """point => {
                    const element = document.elementFromPoint(point.x, point.y)
                    return {
                        className: String(element?.className || ''),
                        text: element?.textContent || '',
                        nodeClassName: String(element?.closest('.react-flow__node')?.className || ''),
                    }
                }""",
                {"x": inputs_before["x"] + 22, "y": inputs_before["y"] + 9},
            )
            page.mouse.move(context_before["x"] + context_before["width"] / 2, context_before["y"] + context_before["height"] / 2)
            page.mouse.down()
            page.mouse.move(context_before["x"] + context_before["width"] / 2 + 90, context_before["y"] + context_before["height"] / 2 + 55, steps=12)
            page.mouse.up()
            inputs_drag_x = inputs_before["x"] + 22
            inputs_drag_y = inputs_before["y"] + 9
            page.mouse.move(inputs_drag_x, inputs_drag_y)
            page.mouse.down()
            page.mouse.move(inputs_drag_x + 75, inputs_drag_y + 40, steps=12)
            page.mouse.up()
            page.wait_for_timeout(400)

            page.screenshot(path=OUT / "graph_interface_edges.png", full_page=True)

            lines = page.locator('[data-testid="sdk.workflow.canvas.line"]').count()
            flow_line = page.locator('[data-line-id="context_exec-out-start-printer_exec-in-enter"]').count()
            data_line = page.locator('[data-line-id="message_data-out-message-printer_data-in-message"]').count()
            exec_glyph_styles = page.evaluate(
                """() => {
                    const styleFor = selector => {
                        const element = document.querySelector(selector)
                        if (!element) return null
                        const style = getComputedStyle(element)
                        return {
                            backgroundColor: style.backgroundColor,
                            borderTopColor: style.borderTopColor,
                        }
                    }
                    return {
                        output: styleFor('[data-blueprint-node="context"] [data-pin-row="exec-out-start"] [data-pin-glyph="true"]'),
                        input: styleFor('[data-blueprint-node="printer"] [data-pin-row="exec-in-enter"] [data-pin-glyph="true"]'),
                    }
                }""",
            )
            exec_input_matches_output = (
                exec_glyph_styles["output"]
                and exec_glyph_styles["input"]
                and exec_glyph_styles["output"]["backgroundColor"] == exec_glyph_styles["input"]["backgroundColor"]
                and exec_glyph_styles["input"]["backgroundColor"] != "rgba(0, 0, 0, 0)"
            )
            context_after = page.locator('[data-blueprint-node="context"]').bounding_box()
            inputs_after = page.locator('[data-blueprint-node="Graph Inputs"]').bounding_box()
            context_moved = context_after and abs(context_after["x"] - context_before["x"]) > 30
            inputs_moved = inputs_after and (
                abs(inputs_after["x"] - inputs_before["x"]) > 30 or
                abs(inputs_after["y"] - inputs_before["y"]) > 30
            )
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("Visual graph interface edge regression")
    print(f"  lines: {lines}")
    print(f"  context flow line: {flow_line}")
    print(f"  parameter data line: {data_line}")
    print(f"  context moved: {context_moved}")
    print(f"  graph inputs moved: {inputs_moved}")
    print(f"  exec glyph styles: {exec_glyph_styles}")
    print(f"  graph inputs hit: {inputs_hit_before}")
    print(f"  graph inputs before: {inputs_before}")
    print(f"  graph inputs after: {inputs_after}")
    print(f"  backend commands: {exec_commands}")
    print(f"Screenshots: {OUT}")
    if lines < 2 or flow_line < 1 or data_line < 1 or not exec_input_matches_output or not context_moved or not inputs_moved or exec_commands:
        raise SystemExit("Expected context and graph parameter edges to render")


if __name__ == "__main__":
    main()
