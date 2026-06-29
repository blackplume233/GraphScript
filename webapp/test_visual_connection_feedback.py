#!/usr/bin/env python3
"""E2E regression for Blueprint-style invalid connection feedback."""
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

OUT = Path(__file__).parent / "test_screenshots" / "visual_connection_feedback"
OUT.mkdir(parents=True, exist_ok=True)


def drag_port(page, source_node, source_port, target_node, target_port):
    source = page.locator(f'[data-blueprint-node="{source_node}"] [data-port-id="{source_port}"]').first
    target = page.locator(f'[data-blueprint-node="{target_node}"] [data-port-id="{target_port}"]').first
    source.wait_for(state="visible", timeout=10000)
    target.wait_for(state="visible", timeout=10000)
    source_box = source.bounding_box()
    target_box = target.bounding_box()
    source_x = source_box["x"] + source_box["width"] / 2
    source_y = source_box["y"] + source_box["height"] / 2
    target_x = target_box["x"] + target_box["width"] / 2
    target_y = target_box["y"] + target_box["height"] / 2
    page.mouse.move(source_x, source_y)
    page.mouse.down()
    page.wait_for_timeout(150)
    page.mouse.move(source_x + 40, source_y, steps=8)
    page.mouse.move((source_x + target_x) / 2, (source_y + target_y) / 2, steps=15)
    page.mouse.move(target_x, target_y, steps=15)
    page.wait_for_timeout(100)
    page.mouse.up()


def main():
    state = visual.initial_state()
    exec_commands = []
    proc = visual.start_vite()
    results = []
    feedback_text = ""
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1400, "height": 900})

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
            page.locator('select[title="Active event/function for visual edge edits"]').select_option("event:BeginPlay")
            page.wait_for_selector('[data-blueprint-node="branch"] [data-port-id="exec-out-onTrue"]', state="visible", timeout=10000)
            page.wait_for_selector('[data-blueprint-node="printer"] [data-port-id="data-in-message"]', state="visible", timeout=10000)

            drag_port(page, "branch", "exec-out-onTrue", "printer", "data-in-message")
            feedback = page.locator('[data-connection-feedback="error"]')
            feedback.wait_for(state="visible", timeout=3000)
            feedback_text = feedback.inner_text(timeout=3000)
            page.screenshot(path=OUT / "01_invalid_connection_feedback.png", full_page=True)

            graph = visual.current_graph(state)
            event = graph["events"][0]
            results = [
                ("active block selected", exec_commands == ["event BeginPlay"]),
                ("feedback shown", "Exec pins connect to exec pins" in feedback_text),
                ("no edge commands replayed", not any(cmd.startswith(("flow ", "link ", "unflow ", "unlink ")) for cmd in exec_commands)),
                ("no flow created", event["flows"] == []),
                ("no data link created", event["links"] == []),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual connection feedback regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Feedback: {feedback_text}")
    print(f"Screenshots: {OUT}")
    if failed:
        print("Commands:")
        for command in exec_commands:
            print(f"  {command}")
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
