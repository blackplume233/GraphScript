#!/usr/bin/env python3
"""E2E regression for canvas edge selection focusing the Properties row."""
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

OUT = Path(__file__).parent / "test_screenshots" / "visual_edge_selection_focus"
OUT.mkdir(parents=True, exist_ok=True)


def source_range(start_line, start_column, end_line, end_column):
    return {
        "start": {"line": start_line, "column": start_column},
        "end": {"line": end_line, "column": end_column},
    }


def state_with_edge_metadata():
    state = delete_replay.state_with_edges()
    event = visual.current_graph(state)["events"][0]
    flow = event["flows"][0]
    flow["id"] = "flow-stable-001"
    flow["persistent_id"] = "flow-persistent-001"
    flow["source_range"] = source_range(8, 9, 8, 42)
    flow["from_endpoint_source_range"] = source_range(8, 14, 8, 27)
    flow["to_endpoint_source_range"] = source_range(8, 31, 8, 44)
    link = event["links"][0]
    link["id"] = "link-stable-001"
    link["persistent_id"] = "link-persistent-001"
    link["source_range"] = source_range(9, 9, 9, 37)
    link["target_endpoint_source_range"] = source_range(9, 9, 9, 24)
    link["source_endpoint_source_range"] = source_range(9, 27, 9, 37)
    return state


def click_line(page, line_id):
    selector = f'[data-line-id="{line_id}"]'
    page.locator(selector).first.wait_for(state="attached", timeout=10000)
    path = page.locator(selector).first.locator('path[d^="M"]').first
    path.wait_for(state="attached", timeout=10000)
    point = path.evaluate("""
        el => {
            const length = el.getTotalLength()
            const local = el.getPointAtLength(length / 2)
            const screen = new DOMPoint(local.x, local.y).matrixTransform(el.getScreenCTM())
            return { x: screen.x, y: screen.y }
        }
    """)
    page.mouse.click(point["x"], point["y"])


def main():
    state = state_with_edge_metadata()
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
            page.wait_for_selector('[data-logic-flow-source="branch.onTrue -> printer.enter"]', timeout=10000)
            page.wait_for_selector('[data-logic-link-source="text.value -> printer.message"]', timeout=10000)
            page.screenshot(path=OUT / "01_before_selection.png", full_page=True)

            click_line(page, "branch_exec-out-onTrue-printer_exec-in-enter")
            flow_row = page.locator(
                '[data-logic-flow-source="branch.onTrue -> printer.enter"]'
                '[data-logic-connection-selected="true"]'
            ).first
            flow_row.wait_for(state="visible", timeout=10000)
            flow_row_focused = flow_row.count() == 1
            flow_redirect_controls = flow_row.locator('[data-action="redirect-flow-source"]').count()
            flow_selected_id = flow_row.get_attribute("data-selected-edge-id")
            flow_selected_persistent_id = flow_row.get_attribute("data-selected-edge-persistent-id")
            flow_selected_source_range = flow_row.get_attribute("data-selected-edge-source-range")
            flow_selected_source_endpoint_range = flow_row.get_attribute("data-selected-edge-source-endpoint-range")

            click_line(page, "text_data-out-value-printer_data-in-message")
            link_row = page.locator(
                '[data-logic-link-source="text.value -> printer.message"]'
                '[data-logic-connection-selected="true"]'
            ).first
            link_row.wait_for(state="visible", timeout=10000)
            link_row_focused = link_row.count() == 1
            link_redirect_controls = link_row.locator('[data-action="redirect-link-source"]').count()
            link_selected_id = link_row.get_attribute("data-selected-edge-id")
            link_selected_persistent_id = link_row.get_attribute("data-selected-edge-persistent-id")
            link_selected_source_range = link_row.get_attribute("data-selected-edge-source-range")
            link_selected_target_endpoint_range = link_row.get_attribute("data-selected-edge-target-endpoint-range")
            flow_selection_cleared = page.locator(
                '[data-logic-flow-source="branch.onTrue -> printer.enter"]'
                '[data-logic-connection-selected="true"]'
            ).count() == 0

            search_box = page.get_by_label("Search graph")
            search_box.fill("flow-persistent-001")
            search_connection = page.locator(
                '[data-graph-search-results] '
                '[data-graph-search-result-kind="connection"]'
                '[data-graph-search-result-label="flow branch.onTrue -> printer.enter"]'
            ).first
            search_connection.wait_for(state="visible", timeout=10000)
            search_connection_detail = search_connection.locator('[data-graph-search-result-detail]').inner_text()
            search_connection.click()
            search_flow_row = page.locator(
                '[data-logic-flow-source="branch.onTrue -> printer.enter"]'
                '[data-logic-connection-selected="true"]'
            ).first
            search_flow_row.wait_for(state="visible", timeout=10000)
            search_flow_selected_id = search_flow_row.get_attribute("data-selected-edge-id")
            search_flow_selected_persistent_id = search_flow_row.get_attribute("data-selected-edge-persistent-id")
            search_flow_selected_source_range = search_flow_row.get_attribute("data-selected-edge-source-range")

            search_box.fill("8:9-8:42")
            range_search_connection = page.locator(
                '[data-graph-search-results] '
                '[data-graph-search-result-kind="connection"]'
                '[data-graph-search-result-label="flow branch.onTrue -> printer.enter"]'
            ).first
            range_search_connection.wait_for(state="visible", timeout=10000)
            range_search_connection_detail = range_search_connection.locator('[data-graph-search-result-detail]').inner_text()
            range_search_connection.click()
            range_search_flow_row = page.locator(
                '[data-logic-flow-source="branch.onTrue -> printer.enter"]'
                '[data-logic-connection-selected="true"]'
            ).first
            range_search_flow_row.wait_for(state="visible", timeout=10000)
            range_search_flow_selected_source_range = range_search_flow_row.get_attribute("data-selected-edge-source-range")

            page.screenshot(path=OUT / "02_after_selection.png", full_page=True)
            mutation_commands = [
                command for command in exec_commands
                if command.startswith(("flow ", "link ", "unflow ", "unlink "))
            ]
            results = [
                ("flow row focused from canvas line", flow_row_focused),
                ("flow selected id carried from canvas payload", flow_selected_id == "flow-stable-001"),
                ("flow persistent id carried from canvas payload", flow_selected_persistent_id == "flow-persistent-001"),
                ("flow source range carried from canvas payload", flow_selected_source_range == "8:9-8:42"),
                ("flow endpoint range carried from canvas payload", flow_selected_source_endpoint_range == "8:14-8:27"),
                ("flow redirect entry visible", flow_redirect_controls == 1),
                ("link row focused from canvas line", link_row_focused),
                ("link selected id carried from canvas payload", link_selected_id == "link-stable-001"),
                ("link persistent id carried from canvas payload", link_selected_persistent_id == "link-persistent-001"),
                ("link source range carried from canvas payload", link_selected_source_range == "9:9-9:37"),
                ("link endpoint range carried from canvas payload", link_selected_target_endpoint_range == "9:9-9:24"),
                ("link redirect entry visible", link_redirect_controls == 1),
                ("previous flow focus cleared", flow_selection_cleared),
                ("graph search finds connection by persistent id", "flow-persistent-001" in search_connection_detail),
                ("graph search connection focuses flow row", search_flow_row.count() == 1),
                ("graph search connection selected id carried", search_flow_selected_id == "flow-stable-001"),
                ("graph search connection selected persistent id carried", search_flow_selected_persistent_id == "flow-persistent-001"),
                ("graph search connection source range carried", search_flow_selected_source_range == "8:9-8:42"),
                ("graph search finds connection by source range", "range 8:9-8:42" in range_search_connection_detail),
                ("graph search source range focuses flow row", range_search_flow_row.count() == 1),
                ("graph search source range selected source range carried", range_search_flow_selected_source_range == "8:9-8:42"),
                ("selection did not replay edge mutations", mutation_commands == []),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual edge selection focus regression")
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
