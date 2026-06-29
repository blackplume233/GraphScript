#!/usr/bin/env python3
"""E2E regression for Blueprint-style selected node distribution replaying CLI commands."""
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

OUT = Path(__file__).parent / "test_screenshots" / "visual_node_distribute_replay"
OUT.mkdir(parents=True, exist_ok=True)


def distribution_state():
    state = visual.initial_state()
    graph = visual.current_graph(state)
    graph["nodes"] = [
        visual.positioned_node("PrintString", "printer_a", 180, 140),
        visual.positioned_node("PrintString", "printer_b", 360, 140),
        visual.positioned_node("PrintString", "printer_c", 900, 140),
    ]
    graph["events"] = [{"name": "BeginPlay", "kind": "event", "annotations": [], "flows": [], "links": []}]
    return state


def node_box(page, node_name):
    node = page.locator(f'[data-blueprint-node="{node_name}"]').first
    node.wait_for(state="visible", timeout=10000)
    box = node.bounding_box()
    if not box:
        raise RuntimeError(f"Node {node_name} has no visible box")
    return box


def gap_between(left_box, right_box):
    return right_box["x"] - (left_box["x"] + left_box["width"])


def main():
    state = distribution_state()
    exec_commands = []
    proc = visual.start_vite()
    results = []
    current_graph_text = ""
    left_gap = right_gap = None
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
            page.wait_for_selector('[data-blueprint-node="printer_a"]', state="visible", timeout=10000)
            page.screenshot(path=OUT / "01_before_distribute.png", full_page=True)

            click_node(page, "printer_a")
            wait_node_selected(page, "printer_a")
            click_node(page, "printer_b", modifiers=["Shift"])
            wait_node_selected(page, "printer_b")
            click_node(page, "printer_c", modifiers=["Shift"])
            wait_node_selected(page, "printer_c")

            page.keyboard.press("Alt+Shift+H")
            page.wait_for_timeout(900)

            a_box = node_box(page, "printer_a")
            b_box = node_box(page, "printer_b")
            c_box = node_box(page, "printer_c")
            left_gap = gap_between(a_box, b_box)
            right_gap = gap_between(b_box, c_box)
            current_graph_text = page.locator('[data-current-graph-source="true"]').input_value(timeout=3000)
            page.screenshot(path=OUT / "02_after_distribute_horizontal.png", full_page=True)

            graph = visual.current_graph(state)
            middle = next(node for node in graph["nodes"] if node["instance"] == "printer_b")
            position = next(annotation for annotation in middle["annotations"] if annotation["name"] == "Position")
            position_args = {arg["name"]: arg["value"] for arg in position["args"]}
            results = [
                ("horizontal distribute command replayed", exec_commands == ["annotate node printer_b Position X=540 Y=140"]),
                ("state middle x distributed", position_args.get("X") == "540"),
                ("state middle y preserved", position_args.get("Y") == "140"),
                ("canvas gaps match", abs(left_gap - right_gap) <= 1.5),
                ("current graph text updated", "[Position(X = 540, Y = 140)]" in current_graph_text and "PrintString printer_b" in current_graph_text),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual node distribute replay regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Gaps: left={left_gap}, right={right_gap}")
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
