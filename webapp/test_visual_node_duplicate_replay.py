#!/usr/bin/env python3
"""E2E regression for Blueprint-style selected node duplication replaying CLI commands."""
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

OUT = Path(__file__).parent / "test_screenshots" / "visual_node_duplicate_replay"
OUT.mkdir(parents=True, exist_ok=True)


def click_node(page, node_name, modifiers=None):
    node = page.locator(f'[data-blueprint-node="{node_name}"]').first
    node.wait_for(state="visible", timeout=10000)
    for modifier in modifiers or []:
        page.keyboard.down(modifier)
    node.click(force=True)
    for modifier in reversed(modifiers or []):
        page.keyboard.up(modifier)


def wait_node_selected(page, node_name):
    page.wait_for_function(
        """nodeName => {
            const card = document.querySelector(`[data-blueprint-node="${nodeName}"]`)
            return card?.closest('.react-flow__node')?.classList.contains('selected')
        }""",
        arg=node_name,
        timeout=3000,
    )


def main():
    state = visual.initial_state()
    exec_commands = []
    proc = visual.start_vite()
    results = []
    current_graph_text = ""
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
            page.wait_for_selector('[data-blueprint-node="branch"]', state="visible", timeout=10000)
            page.screenshot(path=OUT / "01_before_duplicate.png", full_page=True)

            click_node(page, "branch")
            wait_node_selected(page, "branch")
            click_node(page, "printer", modifiers=["Shift"])
            wait_node_selected(page, "printer")

            page.keyboard.press("Control+D")
            page.wait_for_selector('[data-blueprint-node="branch_copy"]', state="visible", timeout=10000)
            page.wait_for_selector('[data-blueprint-node="printer_copy"]', state="visible", timeout=10000)
            page.wait_for_timeout(500)
            current_graph_text = page.locator('[data-current-graph-source="true"]').input_value(timeout=3000)
            page.screenshot(path=OUT / "02_after_duplicate.png", full_page=True)

            graph = visual.current_graph(state)
            node_names = {node["instance"] for node in graph["nodes"]}
            expected_commands = [
                "add_node Branch branch_copy",
                "annotate node branch_copy Position X=228 Y=188",
                "add_node PrintString printer_copy",
                "annotate node printer_copy Position X=608 Y=188",
            ]
            results = [
                ("duplicate commands replayed", exec_commands == expected_commands),
                ("mock state has branch copy", "branch_copy" in node_names),
                ("mock state has printer copy", "printer_copy" in node_names),
                ("canvas shows branch copy", page.locator('[data-blueprint-node="branch_copy"]').count() == 1),
                ("canvas shows printer copy", page.locator('[data-blueprint-node="printer_copy"]').count() == 1),
                ("current graph text has branch copy", "Branch branch_copy" in current_graph_text),
                ("current graph text has printer copy", "PrintString printer_copy" in current_graph_text),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual node duplicate replay regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
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
