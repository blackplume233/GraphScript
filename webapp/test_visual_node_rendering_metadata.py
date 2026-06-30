#!/usr/bin/env python3
"""E2E regression for Blueprint-style node metadata rendering."""
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
from test_visual_node_duplicate_replay import click_node, wait_node_selected

OUT = Path(__file__).parent / "test_screenshots" / "visual_node_rendering_metadata"
OUT.mkdir(parents=True, exist_ok=True)


def enrich_state(state):
    for node_type in state["types"]:
        for pin in node_type.get("pins", []):
            pin.setdefault("annotations", [])
        if node_type["type_name"] == "Branch":
            node_type["tags"] = ["Logic"]
            node_type["source_file"] = "logic.d.gs"
            node_type["annotations"] = []
        if node_type["type_name"] == "PrintString":
            node_type["tags"] = ["IO"]
            node_type["source_file"] = "debug.d.gs"
            node_type["annotations"] = []
            node_type["fields"] = [
                {
                    "name": "message",
                    "type": "FString",
                    "default": "Hello",
                    "source_file": "debug.d.gs",
                    "annotations": [],
                },
                {
                    "name": "duration",
                    "type": "float",
                    "default": "2.0",
                    "source_file": "debug.d.gs",
                    "annotations": [],
                },
            ]


def main():
    state = visual.initial_state()
    enrich_state(state)
    exec_commands = []
    proc = visual.start_vite()
    results = []
    page_errors = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1500, "height": 950})
            page.on("console", lambda msg: page_errors.append(f"console:{msg.type}:{msg.text}") if msg.type in {"warning", "error"} else None)
            page.on("pageerror", lambda exc: page_errors.append(f"pageerror:{exc}"))

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
            try:
                page.wait_for_selector('[data-blueprint-node="branch"]', state="visible", timeout=10000)
            except Exception:
                page.screenshot(path=OUT / "failure_no_branch.png", full_page=True)
                if page_errors:
                    print("Page errors before node render:")
                    for error in page_errors:
                        print(f"  {error}")
                raise
            page.wait_for_selector('[data-node-category="Logic"]', state="visible", timeout=10000)
            page.wait_for_selector('[data-node-category="IO"]', state="visible", timeout=10000)
            page.wait_for_selector('[data-blueprint-node="printer"] [data-node-intrinsic-property="message"]', state="visible", timeout=10000)
            page.screenshot(path=OUT / "01_metadata_rendering.png", full_page=True)

            click_node(page, "branch")
            wait_node_selected(page, "branch")
            click_node(page, "printer", modifiers=["Shift"])
            wait_node_selected(page, "printer")
            page.wait_for_selector('[data-multi-select-outline="true"][data-multi-select-count="2"]', state="visible", timeout=10000)
            outline_rendered_after_multiselect = page.locator('[data-multi-select-outline="true"][data-multi-select-count="2"]').count() == 1
            page.screenshot(path=OUT / "02_multi_select_outline.png", full_page=True)

            click_node(page, "printer")
            page.wait_for_selector('[data-node-intrinsic-property-row="message"]', state="visible", timeout=10000)
            property_row = page.locator('[data-node-intrinsic-property-row="message"]').first
            canvas_property = page.locator('[data-blueprint-node="printer"] [data-node-intrinsic-property="message"]').first
            results = [
                ("logic category rendered", page.locator('[data-node-category="Logic"]').count() >= 1),
                ("io category rendered", page.locator('[data-node-category="IO"]').count() >= 1),
                ("canvas field source marked", canvas_property.get_attribute("data-node-intrinsic-property-source") == "debug.d.gs"),
                ("canvas field default marked", canvas_property.get_attribute("data-node-intrinsic-property-default") == "Hello"),
                ("properties field source marked", property_row.get_attribute("data-node-intrinsic-property-source") == "debug.d.gs"),
                ("properties field default marked", property_row.get_attribute("data-node-intrinsic-property-default") == "Hello"),
                ("properties field text shows default", "default: Hello" in property_row.inner_text(timeout=3000)),
                ("multi-select outline rendered", outline_rendered_after_multiselect),
                ("no metadata rendering commands replayed", exec_commands == []),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual node metadata rendering regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if page_errors:
        print("Page errors:")
        for error in page_errors:
            print(f"  {error}")
    if failed:
        print("Commands:")
        for command in exec_commands:
            print(f"  {command}")
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
