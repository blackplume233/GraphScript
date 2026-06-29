#!/usr/bin/env python3
"""E2E regression for Blueprint-style selected node alignment replaying CLI commands."""
import copy
import json
import sys
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

import test_visual_edge_replay as visual
from test_visual_node_duplicate_replay import click_node, wait_node_selected

OUT = Path(__file__).parent / "test_screenshots" / "visual_node_align_replay"
OUT.mkdir(parents=True, exist_ok=True)


def node_box(page, node_name):
    node = page.locator(f'[data-blueprint-node="{node_name}"]').first
    node.wait_for(state="visible", timeout=10000)
    box = node.bounding_box()
    if not box:
        raise RuntimeError(f"Node {node_name} has no visible box")
    return box


def main():
    state = visual.initial_state()
    exec_commands = []
    proc = visual.start_vite()
    results = []
    current_graph_text = ""
    branch_x = printer_x = None
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1500, "height": 950})

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

            def handle_exec(route):
                payload = json.loads(route.request.post_data or "{}")
                command = payload.get("command", "")
                exec_commands.append(command)
                visual.update_state_for_command(state, command)
                state["dirty"] = True
                state["can_undo"] = True
                state["command_log"] = exec_commands[:]
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "command": command, "state": copy.deepcopy(state)}),
                )

            page.route("**/api/exec", handle_exec)

            page.goto(visual.URL, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_selector('[data-blueprint-node="branch"]', state="visible", timeout=10000)
            page.screenshot(path=OUT / "01_before_align.png", full_page=True)

            click_node(page, "branch")
            wait_node_selected(page, "branch")
            click_node(page, "printer", modifiers=["Shift"])
            wait_node_selected(page, "printer")

            page.keyboard.press("Shift+A")
            page.wait_for_timeout(900)

            branch_box = node_box(page, "branch")
            printer_box = node_box(page, "printer")
            branch_x = branch_box["x"]
            printer_x = printer_box["x"]
            current_graph_text = page.locator('[data-current-graph-source="true"]').input_value(timeout=3000)
            page.screenshot(path=OUT / "02_after_align_left.png", full_page=True)

            graph = visual.current_graph(state)
            printer = next(node for node in graph["nodes"] if node["instance"] == "printer")
            position = next(annotation for annotation in printer["annotations"] if annotation["name"] == "Position")
            position_args = {arg["name"]: arg["value"] for arg in position["args"]}

            results = [
                ("left align command replayed", exec_commands == ["annotate node printer Position X=180 Y=140"]),
                ("state printer x aligned", position_args.get("X") == "180"),
                ("state printer y preserved", position_args.get("Y") == "140"),
                ("canvas x aligned", abs(branch_x - printer_x) <= 1.5),
                ("current graph text updated", "[Position(X = 180, Y = 140)]" in current_graph_text and "PrintString printer" in current_graph_text),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual node align replay regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Canvas x: branch={branch_x}, printer={printer_x}")
    print(f"Screenshots: {OUT}")
    if failed:
        print("Commands:")
        for command in exec_commands:
            print(f"  {command}")
        print("Current graph text:")
        print(current_graph_text)
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
