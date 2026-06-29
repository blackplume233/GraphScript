#!/usr/bin/env python3
"""E2E regression for Blueprint-style compatible pin preview states."""
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

OUT = Path(__file__).parent / "test_screenshots" / "visual_pin_compatibility_preview"
OUT.mkdir(parents=True, exist_ok=True)


def port_selector(node_name, port_id):
    return (
        f'[data-blueprint-node="{node_name}"] '
        f'[data-port-id="{port_id}"][data-testid="sdk.workflow.canvas.node.port"]'
    )


def port_center(page, node_name, port_id):
    locator = page.locator(port_selector(node_name, port_id)).first
    locator.wait_for(state="visible", timeout=10000)
    box = locator.bounding_box()
    return {
        "x": box["x"] + box["width"] / 2,
        "y": box["y"] + box["height"] / 2,
    }


def pin_state(page, node_name, port_id):
    return page.locator(port_selector(node_name, port_id)).first.evaluate(
        "el => el.closest('[data-pin-connection-state]')?.getAttribute('data-pin-connection-state') || ''"
    )


def wait_pin_state(page, node_name, port_id, state):
    selector = port_selector(node_name, port_id)
    page.wait_for_function(
        """([selector, expected]) => {
            const handle = document.querySelector(selector)
            return handle?.closest('[data-pin-connection-state]')
                ?.getAttribute('data-pin-connection-state') === expected
        }""",
        arg=[selector, state],
        timeout=3000,
    )


def drag_until_preview(page, source_node, source_port, toward_node, toward_port):
    source = port_center(page, source_node, source_port)
    target = port_center(page, toward_node, toward_port)
    page.mouse.move(source["x"], source["y"])
    page.mouse.down()
    page.wait_for_timeout(120)
    page.mouse.move(source["x"] + 36, source["y"], steps=8)
    page.mouse.move((source["x"] + target["x"]) / 2, (source["y"] + target["y"]) / 2, steps=14)
    page.wait_for_timeout(120)


def release_and_reload(page):
    page.mouse.up()
    page.wait_for_timeout(180)
    page.goto(visual.URL, wait_until="domcontentloaded", timeout=20000)
    page.locator('select[title="Active event/function for visual edge edits"]').select_option("event:BeginPlay")
    page.wait_for_selector(port_selector("branch", "exec-out-onTrue"), state="visible", timeout=10000)


def main():
    state = visual.initial_state()
    exec_commands = []
    proc = visual.start_vite()
    results = []
    data_states = {}
    exec_states = {}
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
            page.locator('select[title="Active event/function for visual edge edits"]').select_option("event:BeginPlay")
            page.wait_for_selector(port_selector("text", "data-out-value"), state="visible", timeout=10000)
            page.wait_for_selector(port_selector("printer", "data-in-message"), state="visible", timeout=10000)

            drag_until_preview(page, "text", "data-out-value", "printer", "data-in-message")
            wait_pin_state(page, "text", "data-out-value", "origin")
            wait_pin_state(page, "printer", "data-in-message", "compatible")
            wait_pin_state(page, "branch", "data-in-condition", "disabled")
            data_states = {
                "source": pin_state(page, "text", "data-out-value"),
                "compatible": pin_state(page, "printer", "data-in-message"),
                "type_mismatch": pin_state(page, "branch", "data-in-condition"),
                "kind_mismatch": pin_state(page, "branch", "exec-in-enter"),
            }
            page.screenshot(path=OUT / "01_data_pin_preview_states.png", full_page=True)
            release_and_reload(page)

            drag_until_preview(page, "branch", "exec-out-onTrue", "printer", "exec-in-enter")
            wait_pin_state(page, "branch", "exec-out-onTrue", "origin")
            wait_pin_state(page, "printer", "exec-in-enter", "compatible")
            wait_pin_state(page, "printer", "data-in-message", "disabled")
            exec_states = {
                "source": pin_state(page, "branch", "exec-out-onTrue"),
                "compatible": pin_state(page, "printer", "exec-in-enter"),
                "kind_mismatch": pin_state(page, "printer", "data-in-message"),
                "same_direction": pin_state(page, "printer", "exec-out-exit"),
            }
            page.screenshot(path=OUT / "02_exec_pin_preview_states.png", full_page=True)
            page.mouse.up()

            results = [
                ("active block selected", exec_commands[:1] == ["event BeginPlay"]),
                ("data source marked origin", data_states.get("source") == "origin"),
                ("data compatible input highlighted", data_states.get("compatible") == "compatible"),
                ("data type mismatch disabled", data_states.get("type_mismatch") == "disabled"),
                ("data to exec mismatch disabled", data_states.get("kind_mismatch") == "disabled"),
                ("exec source marked origin", exec_states.get("source") == "origin"),
                ("exec compatible input highlighted", exec_states.get("compatible") == "compatible"),
                ("exec to data mismatch disabled", exec_states.get("kind_mismatch") == "disabled"),
                ("same direction exec disabled", exec_states.get("same_direction") == "disabled"),
                ("no edge commands replayed", not any(cmd.startswith(("flow ", "link ", "unflow ", "unlink ")) for cmd in exec_commands)),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual pin compatibility preview regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Data states: {data_states}")
    print(f"Exec states: {exec_states}")
    print(f"Screenshots: {OUT}")
    if failed:
        print("Commands:")
        for command in exec_commands:
            print(f"  {command}")
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
