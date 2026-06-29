#!/usr/bin/env python3
"""E2E regression for reconnecting the source endpoint of visual edges."""
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
import test_visual_edge_redirect_replay as redirect
import test_visual_edge_reconnect_replay as reconnect

OUT = Path(__file__).parent / "test_screenshots" / "visual_edge_source_reconnect_replay"
OUT.mkdir(parents=True, exist_ok=True)


def wait_for_line(page, line_id, timeout=10000):
    try:
        page.wait_for_selector(f'[data-line-id="{line_id}"]', state="attached", timeout=timeout)
        return True
    except Exception:
        return False


def drag_line_source_to_port(page, line_id, node_name, port_id):
    line = page.locator(f'[data-line-id="{line_id}"]').first
    line.wait_for(state="attached", timeout=10000)
    source_handle = line.locator('[data-edge-reconnect-handle="source"]').first
    source_handle.wait_for(state="attached", timeout=10000)
    source_box = source_handle.bounding_box()
    start = {
        "x": source_box["x"] + source_box["width"] / 2,
        "y": source_box["y"] + source_box["height"] / 2,
    }
    target = page.locator(
        f'[data-blueprint-node="{node_name}"] [data-port-id="{port_id}"][data-testid="sdk.workflow.canvas.node.port"]'
    ).first
    target.wait_for(state="visible", timeout=10000)
    target_box = target.bounding_box()
    target_x = target_box["x"] + target_box["width"] / 2
    target_y = target_box["y"] + target_box["height"] / 2
    page.mouse.move(start["x"], start["y"])
    page.mouse.down()
    page.wait_for_timeout(120)
    page.mouse.move((start["x"] + target_x) / 2, (start["y"] + target_y) / 2, steps=18)
    page.mouse.move(target_x, target_y, steps=18)
    page.mouse.up()


def main():
    state = reconnect.state_with_reconnect_annotations()
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
                redirect.normalize_annotations(state)
                reconnect.sync_persistent_ids(state)
                redirect.assign_generated_connection_source_ranges(state)
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
            page.wait_for_selector('[data-line-id="branch_exec-out-onTrue-printer_exec-in-enter"]', state="attached", timeout=10000)
            page.wait_for_selector('[data-line-id="text_data-out-value-printer_data-in-message"]', state="attached", timeout=10000)
            page.wait_for_timeout(500)
            page.screenshot(path=OUT / "01_before_source_reconnect.png", full_page=True)

            drag_line_source_to_port(
                page,
                "branch_exec-out-onTrue-printer_exec-in-enter",
                "printer2",
                "exec-out-exit",
            )
            page.wait_for_timeout(1200)
            flow_line_visible = wait_for_line(page, "printer2_exec-out-exit-printer_exec-in-enter")
            flow_event = visual.current_graph(state)["events"][0]
            flow = flow_event["flows"][0] if flow_event["flows"] else {}
            flow_reconnected = (
                len(flow_event["flows"]) == 1 and
                redirect.has_flow(flow_event, "printer2", "exit", "printer", "enter")
            )

            state = reconnect.state_with_reconnect_annotations()
            page.goto(visual.URL, wait_until="domcontentloaded", timeout=20000)
            page.locator('select[title="Active event/function for visual edge edits"]').select_option("event:BeginPlay")
            page.wait_for_selector('[data-line-id="branch_exec-out-onTrue-printer_exec-in-enter"]', state="attached", timeout=10000)
            page.wait_for_selector('[data-line-id="text_data-out-value-printer_data-in-message"]', state="attached", timeout=10000)
            page.wait_for_timeout(500)

            drag_line_source_to_port(
                page,
                "text_data-out-value-printer_data-in-message",
                "text2",
                "data-out-value",
            )
            page.wait_for_timeout(1200)
            link_line_visible = wait_for_line(page, "text2_data-out-value-printer_data-in-message")
            page.screenshot(path=OUT / "02_after_source_reconnect.png", full_page=True)

            link_event = visual.current_graph(state)["events"][0]
            link = link_event["links"][0] if link_event["links"] else {}
            link_reconnected = (
                len(link_event["links"]) == 1 and
                redirect.has_link(link_event, "printer", "message", "text2", "value")
            )
            line_ids = page.locator('[data-testid="sdk.workflow.canvas.line"]').evaluate_all(
                "els => els.map(e => e.getAttribute('data-line-id')).sort()"
            )
            expected = [
                "event BeginPlay",
                "event BeginPlay",
                "unflow branch.onTrue printer.enter",
                "flow printer2.exit printer.enter",
                "annotate flow event BeginPlay printer2.exit printer.enter Id flow-reset",
                "event BeginPlay",
                "event BeginPlay",
                "unlink printer.message",
                "link printer.message text2.value",
                "annotate link event BeginPlay printer.message text2.value PersistentId Value=link-reset",
            ]
            results = [
                ("active block selected", exec_commands[:1] == ["event BeginPlay"]),
                ("source flow reconnect commands replayed",
                 "unflow branch.onTrue printer.enter" in exec_commands and
                 "flow printer2.exit printer.enter" in exec_commands),
                ("source link reconnect commands replayed",
                 "unlink printer.message" in exec_commands and
                 "link printer.message text2.value" in exec_commands),
                ("command order preserved", exec_commands[:len(expected)] == expected),
                ("state reconnected flow source", flow_reconnected),
                ("state reconnected link source", link_reconnected),
                ("flow annotation migrated", redirect.has_annotation(flow, "Id", "flow-reset")),
                ("link annotation migrated", redirect.has_annotation(link, "PersistentId", "link-reset")),
                ("flow line visible", flow_line_visible),
                ("link line visible", link_line_visible),
                ("no stale original data line", "text_data-out-value-printer_data-in-message" not in line_ids),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual edge source reconnect replay regression")
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
