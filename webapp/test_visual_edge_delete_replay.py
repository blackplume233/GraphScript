#!/usr/bin/env python3
"""E2E regression for visual canvas edge deletion replaying CLI commands."""
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

OUT = Path(__file__).parent / "test_screenshots" / "visual_edge_delete_replay"
OUT.mkdir(parents=True, exist_ok=True)


def state_with_edges():
    state = visual.initial_state()
    event = visual.current_graph(state)["events"][0]
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


def line_midpoint(page, line_id):
    selector = f'[data-line-id="{line_id}"]'
    page.locator(selector).first.wait_for(state="attached", timeout=10000)
    line = page.locator(selector).first
    path = line.locator('path[d^="M"]').first
    path.wait_for(state="attached", timeout=10000)
    point = path.evaluate("""
        el => {
            const length = el.getTotalLength()
            const local = el.getPointAtLength(length / 2)
            const screen = new DOMPoint(local.x, local.y).matrixTransform(el.getScreenCTM())
            return { x: screen.x, y: screen.y }
        }
    """)
    return selector, point


def select_line_midpoint(page, line_id):
    selector, point = line_midpoint(page, line_id)
    page.mouse.click(point["x"], point["y"])
    page.wait_for_timeout(300)
    return selector


def delete_line_with_button(page, line_id):
    selector = select_line_midpoint(page, line_id)
    button = page.locator('[data-edge-delete-button]').first
    button.wait_for(state="visible", timeout=3000)
    visible = button.is_visible()
    button.click()
    page.wait_for_selector(selector, state="detached", timeout=3000)
    return visible


def delete_line_with_key(page, line_id):
    selector = select_line_midpoint(page, line_id)
    for key in ("Delete", "Backspace", "Delete"):
        page.keyboard.press(key)
        try:
            page.wait_for_selector(selector, state="detached", timeout=1200)
            return
        except Exception:
            pass


def delete_line_with_alt_click(page, line_id):
    selector, point = line_midpoint(page, line_id)
    page.keyboard.down("Alt")
    try:
        page.mouse.click(point["x"], point["y"])
    finally:
        page.keyboard.up("Alt")
    page.wait_for_selector(selector, state="detached", timeout=3000)


def main():
    state = state_with_edges()
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
            page.screenshot(path=OUT / "01_before_delete.png", full_page=True)

            delete_button_visible = delete_line_with_button(page, "branch_exec-out-onTrue-printer_exec-in-enter")
            page.wait_for_timeout(1200)
            flow_event = visual.current_graph(state)["events"][0]
            flow_deleted = len(flow_event["flows"]) == 0

            state = state_with_edges()
            page.goto(visual.URL, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_selector('[data-line-id="branch_exec-out-onTrue-printer_exec-in-enter"]', state="attached", timeout=10000)
            page.wait_for_selector('[data-line-id="text_data-out-value-printer_data-in-message"]', state="attached", timeout=10000)
            page.locator('select[title="Active event/function for visual edge edits"]').select_option("event:BeginPlay")
            page.wait_for_timeout(500)

            delete_line_with_alt_click(page, "text_data-out-value-printer_data-in-message")
            page.wait_for_timeout(1200)
            page.screenshot(path=OUT / "02_after_delete.png", full_page=True)
            link_event = visual.current_graph(state)["events"][0]
            link_deleted = len(link_event["links"]) == 0
            expected = [
                "event BeginPlay",
                "event BeginPlay",
                "unflow branch.onTrue printer.enter",
                "event BeginPlay",
                "event BeginPlay",
                "unlink printer.message",
            ]
            results = [
                ("active block selected", exec_commands[:1] == ["event BeginPlay"]),
                ("visual unflow command replayed", "unflow branch.onTrue printer.enter" in exec_commands),
                ("visual unlink command replayed", "unlink printer.message" in exec_commands),
                ("selected edge delete button visible", delete_button_visible),
                ("command order preserved", exec_commands[:len(expected)] == expected),
                ("state removed flow", flow_deleted),
                ("state removed link", link_deleted),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual edge delete replay regression")
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
