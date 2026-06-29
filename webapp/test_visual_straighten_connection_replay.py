#!/usr/bin/env python3
"""E2E regression for Blueprint-style Q straighten connection replaying node position."""
import copy
import json
import sys
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

import test_visual_edge_delete_replay as delete_replay
import test_visual_edge_replay as visual
from test_visual_edge_selection_focus import click_line

OUT = Path(__file__).parent / "test_screenshots" / "visual_straighten_connection_replay"
OUT.mkdir(parents=True, exist_ok=True)


def set_node_position(state, node_name, x, y):
    for node in visual.current_graph(state)["nodes"]:
        if node["instance"] != node_name:
            continue
        node["annotations"] = [
            annotation for annotation in node["annotations"]
            if annotation["name"] != "Position"
        ]
        node["annotations"].append({
            "name": "Position",
            "args": [
                {"name": "X", "value": str(x)},
                {"name": "Y", "value": str(y)},
            ],
        })
        return
    raise RuntimeError(f"Node {node_name} not found")


def node_center_y(page, node_name):
    node = page.locator(f'[data-blueprint-node="{node_name}"]').first
    node.wait_for(state="visible", timeout=10000)
    box = node.bounding_box()
    if not box:
        raise RuntimeError(f"Node {node_name} has no visible box")
    return box["y"] + box["height"] / 2


def main():
    state = delete_replay.state_with_edges()
    set_node_position(state, "branch", 180, 140)
    set_node_position(state, "printer", 560, 320)
    exec_commands = []
    proc = visual.start_vite()
    results = []
    current_graph_text = ""
    before_delta = after_delta = None
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
            page.wait_for_selector('[data-line-id="branch_exec-out-onTrue-printer_exec-in-enter"]', state="attached", timeout=10000)
            page.screenshot(path=OUT / "01_before_straighten.png", full_page=True)

            before_delta = abs(node_center_y(page, "branch") - node_center_y(page, "printer"))
            click_line(page, "branch_exec-out-onTrue-printer_exec-in-enter")
            page.wait_for_selector('[data-logic-flow-source="branch.onTrue -> printer.enter"][data-logic-connection-selected="true"]', state="visible", timeout=10000)
            page.keyboard.press("q")
            page.wait_for_timeout(900)

            after_delta = abs(node_center_y(page, "branch") - node_center_y(page, "printer"))
            current_graph_text = page.locator('[data-current-graph-source="true"]').input_value(timeout=3000)
            page.screenshot(path=OUT / "02_after_straighten.png", full_page=True)

            graph = visual.current_graph(state)
            printer = next(node for node in graph["nodes"] if node["instance"] == "printer")
            position = next(annotation for annotation in printer["annotations"] if annotation["name"] == "Position")
            position_args = {arg["name"]: arg["value"] for arg in position["args"]}
            results = [
                ("edge selection did not mutate graph", not any(command.startswith(("flow ", "link ", "unflow ", "unlink ")) for command in exec_commands)),
                ("straighten command replayed", "annotate node printer Position X=560 Y=140" in exec_commands),
                ("state target y aligned", position_args.get("Y") == "140"),
                ("canvas centers aligned", before_delta > 100 and after_delta <= 1.5),
                ("current graph text updated", "[Position(X = 560, Y = 140)]" in current_graph_text and "PrintString printer" in current_graph_text),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual straighten connection replay regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Center delta: before={before_delta}, after={after_delta}")
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
