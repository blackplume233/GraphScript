#!/usr/bin/env python3
"""E2E regression for visual edge reconnect replaying transactional CLI commands."""
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
import test_visual_edge_reset_guard as reset_guard

OUT = Path(__file__).parent / "test_screenshots" / "visual_edge_reconnect_replay"
OUT.mkdir(parents=True, exist_ok=True)


def wait_for_line(page, line_id, timeout=10000):
    try:
        page.wait_for_selector(f'[data-line-id="{line_id}"]', state="attached", timeout=timeout)
        return True
    except Exception:
        return False


def state_with_reconnect_annotations():
    state = reset_guard.state_with_reset_targets()
    event = visual.current_graph(state)["events"][0]
    event["flows"][0]["id"] = "flow-old-topology"
    event["flows"][0]["persistent_id"] = "flow-reset"
    event["flows"][0]["source_range"] = redirect.source_range(8, 9, 8, 42)
    event["flows"][0]["from_endpoint_source_range"] = redirect.source_range(8, 14, 8, 27)
    event["flows"][0]["to_endpoint_source_range"] = redirect.source_range(8, 31, 8, 44)
    event["flows"][0]["annotations"] = [{
        "name": "Id",
        "args": [{"name": "", "value": "flow-reset"}],
    }]
    event["links"][0]["id"] = "link-old-topology"
    event["links"][0]["persistent_id"] = "link-reset"
    event["links"][0]["source_range"] = redirect.source_range(9, 9, 9, 37)
    event["links"][0]["target_endpoint_source_range"] = redirect.source_range(9, 9, 9, 24)
    event["links"][0]["source_endpoint_source_range"] = redirect.source_range(9, 27, 9, 37)
    event["links"][0]["annotations"] = [{
        "name": "PersistentId",
        "args": [{"name": "Value", "value": "link-reset"}],
    }]
    return state


def persistent_id_from_annotations(annotations):
    for annotation in annotations:
        if annotation.get("name") not in ("Id", "PersistentId"):
            continue
        for arg in annotation.get("args", []):
            name = arg.get("name", "")
            if name in ("", "value", "Value"):
                return arg.get("value", "")
    return ""


def sync_persistent_ids(state):
    for graph in state["module"]["graphs"]:
        for event in graph.get("events", []):
            for flow in event.get("flows", []):
                flow["persistent_id"] = persistent_id_from_annotations(flow.get("annotations", []))
            for link in event.get("links", []):
                link["persistent_id"] = persistent_id_from_annotations(link.get("annotations", []))
        for function in graph.get("functions", []):
            for flow in function.get("flows", []):
                flow["persistent_id"] = persistent_id_from_annotations(flow.get("annotations", []))
            for link in function.get("links", []):
                link["persistent_id"] = persistent_id_from_annotations(link.get("annotations", []))


def main():
    state = state_with_reconnect_annotations()
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
                sync_persistent_ids(state)
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
            page.screenshot(path=OUT / "01_before_reconnect.png", full_page=True)

            reset_guard.drag_line_target_to_port(
                page,
                "branch_exec-out-onTrue-printer_exec-in-enter",
                "printer2",
                "exec-in-enter",
            )
            page.wait_for_timeout(1200)
            flow_line_visible = wait_for_line(page, "branch_exec-out-onTrue-printer2_exec-in-enter")
            flow_event = visual.current_graph(state)["events"][0]
            flow = flow_event["flows"][0] if flow_event["flows"] else {}
            flow_reconnected = (
                len(flow_event["flows"]) == 1 and
                redirect.has_flow(flow_event, "branch", "onTrue", "printer2", "enter")
            )
            flow_row = page.locator(
                '[data-logic-flow-source="branch.onTrue -> printer2.enter"]'
                '[data-logic-connection-selected="true"]'
            ).first
            flow_row.wait_for(state="visible", timeout=10000)
            flow_selected_persistent_id = flow_row.get_attribute("data-selected-edge-persistent-id")
            flow_selected_source_range = flow_row.get_attribute("data-selected-edge-source-range")
            flow_selected_source_endpoint_range = flow_row.get_attribute("data-selected-edge-source-endpoint-range")
            flow_selected_target_endpoint_range = flow_row.get_attribute("data-selected-edge-target-endpoint-range")

            state = state_with_reconnect_annotations()
            page.goto(visual.URL, wait_until="domcontentloaded", timeout=20000)
            page.locator('select[title="Active event/function for visual edge edits"]').select_option("event:BeginPlay")
            page.wait_for_selector('[data-line-id="branch_exec-out-onTrue-printer_exec-in-enter"]', state="attached", timeout=10000)
            page.wait_for_selector('[data-line-id="text_data-out-value-printer_data-in-message"]', state="attached", timeout=10000)
            page.wait_for_timeout(500)

            reset_guard.drag_line_target_to_port(
                page,
                "text_data-out-value-printer_data-in-message",
                "printer2",
                "data-in-message",
            )
            page.wait_for_timeout(1200)
            link_line_visible = wait_for_line(page, "text_data-out-value-printer2_data-in-message")
            page.screenshot(path=OUT / "02_after_reconnect.png", full_page=True)

            link_event = visual.current_graph(state)["events"][0]
            link = link_event["links"][0] if link_event["links"] else {}
            link_reconnected = (
                len(link_event["links"]) == 1 and
                redirect.has_link(link_event, "printer2", "message", "text", "value")
            )
            link_row = page.locator(
                '[data-logic-link-source="text.value -> printer2.message"]'
                '[data-logic-connection-selected="true"]'
            ).first
            link_row.wait_for(state="visible", timeout=10000)
            link_selected_persistent_id = link_row.get_attribute("data-selected-edge-persistent-id")
            link_selected_source_range = link_row.get_attribute("data-selected-edge-source-range")
            link_selected_source_endpoint_range = link_row.get_attribute("data-selected-edge-source-endpoint-range")
            link_selected_target_endpoint_range = link_row.get_attribute("data-selected-edge-target-endpoint-range")
            line_ids = page.locator('[data-testid="sdk.workflow.canvas.line"]').evaluate_all(
                "els => els.map(e => e.getAttribute('data-line-id')).sort()"
            )
            expected = [
                "event BeginPlay",
                "event BeginPlay",
                "unflow branch.onTrue printer.enter",
                "flow branch.onTrue printer2.enter",
                "annotate flow event BeginPlay branch.onTrue printer2.enter Id flow-reset",
                "event BeginPlay",
                "event BeginPlay",
                "unlink printer.message",
                "link printer2.message text.value",
                "annotate link event BeginPlay printer2.message text.value PersistentId Value=link-reset",
            ]
            results = [
                ("active block selected", exec_commands[:1] == ["event BeginPlay"]),
                ("flow reconnect commands replayed",
                 "unflow branch.onTrue printer.enter" in exec_commands and
                 "flow branch.onTrue printer2.enter" in exec_commands),
                ("link reconnect commands replayed",
                 "unlink printer.message" in exec_commands and
                 "link printer2.message text.value" in exec_commands),
                ("command order preserved", exec_commands[:len(expected)] == expected),
                ("state reconnected flow target", flow_reconnected),
                ("state reconnected link target", link_reconnected),
                ("flow annotation migrated", redirect.has_annotation(flow, "Id", "flow-reset")),
                ("link annotation migrated", redirect.has_annotation(link, "PersistentId", "link-reset")),
                ("flow selection migrated by persistent id", flow_selected_persistent_id == "flow-reset"),
                ("link selection migrated by persistent id", link_selected_persistent_id == "link-reset"),
                ("flow selected source range refreshed", flow_selected_source_range == "18:9-18:43"),
                ("flow selected source endpoint range refreshed", flow_selected_source_endpoint_range == "18:14-18:27"),
                ("flow selected target endpoint range refreshed", flow_selected_target_endpoint_range == "18:31-18:45"),
                ("link selected source range refreshed", link_selected_source_range == "20:9-20:41"),
                ("link selected source endpoint range refreshed", link_selected_source_endpoint_range == "20:28-20:38"),
                ("link selected target endpoint range refreshed", link_selected_target_endpoint_range == "20:9-20:25"),
                ("flow line visible", flow_line_visible),
                ("link line visible", link_line_visible),
                ("command log visible", all(page.locator(f"text={command}").count() > 0 for command in expected)),
                ("no stale original lines",
                 "text_data-out-value-printer_data-in-message" not in line_ids),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual edge reconnect replay regression")
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
