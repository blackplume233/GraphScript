#!/usr/bin/env python3
"""E2E regression for rejecting invalid visual edge reset/reconnect gestures."""
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

OUT = Path(__file__).parent / "test_screenshots" / "visual_edge_reset_guard"
OUT.mkdir(parents=True, exist_ok=True)


def state_with_reset_targets():
    state = visual.initial_state()
    graph = visual.current_graph(state)
    graph["nodes"].append(visual.positioned_node("PrintString", "printer2", 900, 140))
    graph["nodes"].append(visual.positioned_node("StringSource", "text2", 900, 360))
    event = graph["events"][0]
    event["flows"] = [{
        "from_node": "branch",
        "from_pin": "onTrue",
        "to_node": "printer",
        "to_pin": "enter",
        "annotations": [],
    }]
    event["links"] = [{
        "target_node": "printer",
        "target_pin": "message",
        "source_node": "text",
        "source_pin": "value",
        "annotations": [],
    }]
    return state


def drag_line_target_to_port(page, line_id, node_name, port_id):
    line = page.locator(f'[data-line-id="{line_id}"]').first
    line.wait_for(state="attached", timeout=10000)
    path = line.locator('path[d^="M"]').first
    path.wait_for(state="attached", timeout=10000)
    start = path.evaluate("""
        el => {
            const length = el.getTotalLength()
            const local = el.getPointAtLength(length * 0.92)
            const screen = new DOMPoint(local.x, local.y).matrixTransform(el.getScreenCTM())
            return { x: screen.x, y: screen.y }
        }
    """)
    target = page.locator(
        f'.node-card:has-text("{node_name}") [data-port-id="{port_id}"] '
        '[data-testid="sdk.workflow.canvas.node.port"]'
    ).first
    target.wait_for(state="visible", timeout=10000)
    target_box = target.bounding_box()
    page.mouse.move(start["x"], start["y"])
    page.mouse.down()
    page.wait_for_timeout(100)
    page.mouse.move(target_box["x"] + target_box["width"] / 2, target_box["y"] + target_box["height"] / 2, steps=25)
    page.mouse.up()


def main():
    state = state_with_reset_targets()
    exec_commands = []
    proc = visual.start_vite()
    results = []
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
            page.wait_for_selector('[data-line-id="text_data-out-value-printer_data-in-message"]', state="attached", timeout=10000)
            page.locator('select[title="Active event/function for visual edge edits"]').select_option("event:BeginPlay")
            page.wait_for_timeout(500)
            page.screenshot(path=OUT / "01_before_reset_attempts.png", full_page=True)

            drag_line_target_to_port(page, "branch_exec-out-onTrue-printer_exec-in-enter", "printer", "data-in-message")
            page.wait_for_timeout(1000)
            drag_line_target_to_port(page, "text_data-out-value-printer_data-in-message", "branch", "exec-in-enter")
            page.wait_for_timeout(1000)
            page.screenshot(path=OUT / "02_after_reset_attempts.png", full_page=True)

            event = visual.current_graph(state)["events"][0]
            line_ids = page.locator('[data-testid="sdk.workflow.canvas.line"]').evaluate_all(
                "els => els.map(e => e.getAttribute('data-line-id')).sort()"
            )
            results = [
                ("active block selected", exec_commands == ["event BeginPlay"]),
                ("no invalid reset commands replayed", not any(cmd.startswith(("flow ", "link ", "unflow ", "unlink ")) for cmd in exec_commands)),
                ("original flow preserved", event["flows"] == [{
                    "from_node": "branch",
                    "from_pin": "onTrue",
                    "to_node": "printer",
                    "to_pin": "enter",
                    "annotations": [],
                }]),
                ("original link preserved", event["links"] == [{
                    "target_node": "printer",
                    "target_pin": "message",
                    "source_node": "text",
                    "source_pin": "value",
                    "annotations": [],
                }]),
                ("no extra visual lines", line_ids == [
                    "branch_exec-out-onTrue-printer_exec-in-enter",
                    "text_data-out-value-printer_data-in-message",
                ]),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual edge reset guard regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        print("Commands:")
        for command in exec_commands:
            print(f"  {command}")
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
