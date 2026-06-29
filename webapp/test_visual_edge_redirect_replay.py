#!/usr/bin/env python3
"""E2E regression for inline edge endpoint redirects replaying CLI commands."""
import copy
import json
import sys
import time
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

import test_visual_edge_replay as visual

OUT = Path(__file__).parent / "test_screenshots" / "visual_edge_redirect_replay"
OUT.mkdir(parents=True, exist_ok=True)


def source_range(start_line, start_column, end_line, end_column):
    return {
        "start": {"line": start_line, "column": start_column},
        "end": {"line": end_line, "column": end_column},
    }


def state_with_redirect_targets():
    state = visual.initial_state()
    graph = visual.current_graph(state)
    graph["nodes"].append(visual.positioned_node("PrintString", "printer2", 900, 140))
    graph["nodes"].append(visual.positioned_node("StringSource", "text2", 900, 360))
    event = graph["events"][0]
    event["annotations"] = []
    event["flows"] = [{
        "id": "flow-old-topology",
        "persistent_id": "flow-keep",
        "source_range": source_range(8, 9, 8, 42),
        "from_endpoint_source_range": source_range(8, 14, 8, 27),
        "to_endpoint_source_range": source_range(8, 31, 8, 44),
        "from_node": "branch",
        "from_pin": "onTrue",
        "to_node": "printer",
        "to_pin": "enter",
        "annotations": [{
            "name": "Id",
            "args": [{"name": "", "value": "flow-keep"}],
        }],
    }]
    event["links"] = [{
        "id": "link-old-topology",
        "persistent_id": "link-keep",
        "source_range": source_range(9, 9, 9, 37),
        "target_endpoint_source_range": source_range(9, 9, 9, 24),
        "source_endpoint_source_range": source_range(9, 27, 9, 37),
        "target_node": "printer",
        "target_pin": "message",
        "source_node": "text",
        "source_pin": "value",
        "annotations": [{
            "name": "PersistentId",
            "args": [{"name": "Value", "value": "link-keep"}],
        }],
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


def assign_generated_connection_source_ranges(state):
    graph = visual.current_graph(state)
    for event in graph.get("events", []):
        for flow in event.get("flows", []):
            if (
                flow.get("from_node") == "branch" and
                flow.get("from_pin") == "onTrue" and
                flow.get("to_node") == "printer2" and
                flow.get("to_pin") == "enter"
            ):
                flow["id"] = "flow-new-topology"
                flow["source_range"] = source_range(18, 9, 18, 43)
                flow["from_endpoint_source_range"] = source_range(18, 14, 18, 27)
                flow["to_endpoint_source_range"] = source_range(18, 31, 18, 45)
            if (
                flow.get("from_node") == "branch" and
                flow.get("from_pin") == "onTrue" and
                flow.get("to_node") == "printer" and
                flow.get("to_pin") == "enter"
            ):
                flow.setdefault("source_range", source_range(8, 9, 8, 42))
        for link in event.get("links", []):
            if (
                link.get("source_node") == "text2" and
                link.get("source_pin") == "value" and
                link.get("target_node") == "printer" and
                link.get("target_pin") == "message"
            ):
                link["id"] = "link-new-source-topology"
                link["source_range"] = source_range(19, 9, 19, 38)
                link["target_endpoint_source_range"] = source_range(19, 9, 19, 24)
                link["source_endpoint_source_range"] = source_range(19, 27, 19, 38)
            if (
                link.get("source_node") == "text" and
                link.get("source_pin") == "value" and
                link.get("target_node") == "printer2" and
                link.get("target_pin") == "message"
            ):
                link["id"] = "link-new-target-topology"
                link["source_range"] = source_range(20, 9, 20, 41)
                link["target_endpoint_source_range"] = source_range(20, 9, 20, 25)
                link["source_endpoint_source_range"] = source_range(20, 28, 20, 38)


def wait_for_command(page, commands, command, timeout_ms=5000):
    deadline = time.time() + timeout_ms / 1000
    while time.time() < deadline:
        if command in commands:
            return True
        page.wait_for_timeout(50)
    return command in commands


def has_flow(event, source_node, source_pin, target_node, target_pin):
    return any(
        flow["from_node"] == source_node and
        flow["from_pin"] == source_pin and
        flow["to_node"] == target_node and
        flow["to_pin"] == target_pin
        for flow in event["flows"]
    )


def has_link(event, target_node, target_pin, source_node, source_pin):
    return any(
        link["target_node"] == target_node and
        link["target_pin"] == target_pin and
        link["source_node"] == source_node and
        link["source_pin"] == source_pin
        for link in event["links"]
    )


def has_annotation(connection, name, value):
    return any(
        annotation["name"] == name and
        any(arg["value"] == value for arg in annotation.get("args", []))
        for annotation in connection.get("annotations", [])
    )


def normalize_annotations(state):
    for graph in state["module"]["graphs"]:
        graph.setdefault("annotations", [])
        for node in graph.get("nodes", []):
            node.setdefault("annotations", [])
        for event in graph.get("events", []):
            event.setdefault("annotations", [])
            for flow in event.get("flows", []):
                flow.setdefault("annotations", [])
            for link in event.get("links", []):
                link.setdefault("annotations", [])
        for function in graph.get("functions", []):
            function.setdefault("annotations", [])
            for flow in function.get("flows", []):
                flow.setdefault("annotations", [])
            for link in function.get("links", []):
                link.setdefault("annotations", [])


def submit_redirect(page, row_selector, action, endpoint):
    form = page.locator(f'{row_selector} [data-action="{action}"]').first
    form.wait_for(state="visible", timeout=10000)
    form.locator("input").fill(endpoint)
    form.locator('button[type="submit"]').click()


def main():
    state = state_with_redirect_targets()
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
                normalize_annotations(state)
                sync_persistent_ids(state)
                assign_generated_connection_source_ranges(state)
                state["dirty"] = True
                state["can_undo"] = True
                state["command_log"] = exec_commands[:]
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "command": command, "state": copy.deepcopy(state)}),
                )

            page.route("**/api/exec", handle_exec)

            page.goto(visual.URL, wait_until="networkidle", timeout=20000)
            page.wait_for_selector('[data-logic-flow-source="branch.onTrue -> printer.enter"]', timeout=10000)
            page.wait_for_selector('[data-logic-link-source="text.value -> printer.message"]', timeout=10000)
            page.screenshot(path=OUT / "01_before_redirect.png", full_page=True)

            submit_redirect(
                page,
                '[data-logic-flow-source="branch.onTrue -> printer.enter"]',
                "redirect-flow-target",
                "printer2.enter",
            )
            flow_redirect_recorded = (
                wait_for_command(page, exec_commands, "event BeginPlay") and
                wait_for_command(page, exec_commands, "unflow branch.onTrue printer.enter") and
                wait_for_command(page, exec_commands, "flow branch.onTrue printer2.enter")
            )
            page.wait_for_selector('[data-logic-flow-source="branch.onTrue -> printer2.enter"]', timeout=10000)
            flow_row = page.locator(
                '[data-logic-flow-source="branch.onTrue -> printer2.enter"]'
                '[data-logic-connection-selected="true"]'
            ).first
            flow_row.wait_for(state="visible", timeout=10000)
            flow_selected_persistent_id = flow_row.get_attribute("data-selected-edge-persistent-id")
            flow_selected_source_range = flow_row.get_attribute("data-selected-edge-source-range")
            flow_selected_source_endpoint_range = flow_row.get_attribute("data-selected-edge-source-endpoint-range")
            flow_selected_target_endpoint_range = flow_row.get_attribute("data-selected-edge-target-endpoint-range")

            submit_redirect(
                page,
                '[data-logic-link-source="text.value -> printer.message"]',
                "redirect-link-source",
                "text2.value",
            )
            link_redirect_recorded = (
                wait_for_command(page, exec_commands, "unlink printer.message") and
                wait_for_command(page, exec_commands, "link printer.message text2.value")
            )
            page.wait_for_selector('[data-logic-link-source="text2.value -> printer.message"]', timeout=10000)
            link_row = page.locator(
                '[data-logic-link-source="text2.value -> printer.message"]'
                '[data-logic-connection-selected="true"]'
            ).first
            link_row.wait_for(state="visible", timeout=10000)
            link_selected_persistent_id = link_row.get_attribute("data-selected-edge-persistent-id")
            link_selected_source_range = link_row.get_attribute("data-selected-edge-source-range")
            link_selected_source_endpoint_range = link_row.get_attribute("data-selected-edge-source-endpoint-range")
            link_selected_target_endpoint_range = link_row.get_attribute("data-selected-edge-target-endpoint-range")
            page.screenshot(path=OUT / "02_after_redirect.png", full_page=True)

            event = visual.current_graph(state)["events"][0]
            flow = event["flows"][0] if event["flows"] else {}
            link = event["links"][0] if event["links"] else {}
            expected = [
                "event BeginPlay",
                "unflow branch.onTrue printer.enter",
                "flow branch.onTrue printer2.enter",
                'annotate flow event BeginPlay branch.onTrue printer2.enter Id "flow-keep"',
                "event BeginPlay",
                "unlink printer.message",
                "link printer.message text2.value",
                'annotate link event BeginPlay printer.message text2.value PersistentId Value="link-keep"',
            ]
            results = [
                ("flow redirect commands replayed", flow_redirect_recorded),
                ("link redirect commands replayed", link_redirect_recorded),
                ("command order preserved", exec_commands[:len(expected)] == expected),
                ("state redirected flow target",
                 len(event["flows"]) == 1 and has_flow(event, "branch", "onTrue", "printer2", "enter")),
                ("state redirected link source",
                 len(event["links"]) == 1 and has_link(event, "printer", "message", "text2", "value")),
                ("flow annotation migrated", has_annotation(flow, "Id", "flow-keep")),
                ("link annotation migrated", has_annotation(link, "PersistentId", "link-keep")),
                ("flow redirect selection migrated by persistent id", flow_selected_persistent_id == "flow-keep"),
                ("link redirect selection migrated by persistent id", link_selected_persistent_id == "link-keep"),
                ("flow redirect selected source range refreshed", flow_selected_source_range == "18:9-18:43"),
                ("flow redirect selected source endpoint range refreshed", flow_selected_source_endpoint_range == "18:14-18:27"),
                ("flow redirect selected target endpoint range refreshed", flow_selected_target_endpoint_range == "18:31-18:45"),
                ("link redirect selected source range refreshed", link_selected_source_range == "19:9-19:38"),
                ("link redirect selected source endpoint range refreshed", link_selected_source_endpoint_range == "19:27-19:38"),
                ("link redirect selected target endpoint range refreshed", link_selected_target_endpoint_range == "19:9-19:24"),
                ("redirected rows visible",
                 page.locator('[data-logic-flow-source="branch.onTrue -> printer2.enter"]').count() == 1 and
                 page.locator('[data-logic-link-source="text2.value -> printer.message"]').count() == 1),
                ("command log visible", all(page.locator(f"text={command}").count() > 0 for command in expected)),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual edge redirect replay regression")
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
