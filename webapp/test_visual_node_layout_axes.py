#!/usr/bin/env python3
"""E2E regression for Blueprint-style layout shortcuts across axes."""
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
from test_visual_node_align_replay import node_box

OUT = Path(__file__).parent / "test_screenshots" / "visual_node_layout_axes"
OUT.mkdir(parents=True, exist_ok=True)


def node_position(state, instance_name):
    graph = visual.current_graph(state)
    node = next(item for item in graph["nodes"] if item["instance"] == instance_name)
    position = next(annotation for annotation in node["annotations"] if annotation["name"] == "Position")
    args = {arg["name"]: arg["value"] for arg in position["args"]}
    return {"x": int(args["X"]), "y": int(args["Y"])}


def layout_state():
    state = visual.initial_state()
    graph = visual.current_graph(state)
    graph["nodes"] = [
        visual.positioned_node("PrintString", "branch", 180, 120),
        visual.positioned_node("PrintString", "printer", 560, 300),
        visual.positioned_node("PrintString", "text", 300, 760),
    ]
    graph["events"] = [{"name": "BeginPlay", "kind": "event", "annotations": [], "flows": [], "links": []}]
    return state


def select_pair(page):
    click_node(page, "branch")
    wait_node_selected(page, "branch")
    click_node(page, "printer", modifiers=["Shift"])
    wait_node_selected(page, "printer")


def select_triple(page):
    click_node(page, "branch")
    wait_node_selected(page, "branch")
    click_node(page, "printer", modifiers=["Shift"])
    wait_node_selected(page, "printer")
    click_node(page, "text", modifiers=["Shift"])
    wait_node_selected(page, "text")


def run_alignment_case(page, state_ref, exec_commands, key, check_name, check):
    state_ref["value"] = layout_state()
    exec_commands.clear()
    page.reload(wait_until="domcontentloaded", timeout=20000)
    page.wait_for_selector('[data-blueprint-node="branch"]', state="visible", timeout=10000)
    select_pair(page)
    page.keyboard.press(key)
    page.wait_for_timeout(500)
    branch_box = node_box(page, "branch")
    printer_box = node_box(page, "printer")
    page.screenshot(path=OUT / f"align_{check_name}.png", full_page=True)
    branch_pos = node_position(state_ref["value"], "branch")
    printer_pos = node_position(state_ref["value"], "printer")
    return [
        (f"{check_name} command replayed", any(cmd.startswith("annotate node") for cmd in exec_commands)),
        (f"{check_name} state changed", branch_pos != {"x": 180, "y": 120} or printer_pos != {"x": 560, "y": 300}),
        (f"{check_name} canvas aligned", check(branch_box, printer_box)),
    ]


def main():
    state_ref = {"value": layout_state()}
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
                body=json.dumps(state_ref["value"]),
            ))
            page.route("**/api/diagnostics", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({"ok": True, "stage": "session", "diagnostics": []}),
            ))
            page.route("**/api/emit", lambda route: route.fulfill(
                status=200,
                content_type="text/plain",
                body=visual.source_from_state(state_ref["value"]),
            ))

            def handle_exec(route):
                payload = json.loads(route.request.post_data or "{}")
                command = payload.get("command", "")
                exec_commands.append(command)
                visual.update_state_for_command(state_ref["value"], command)
                state_ref["value"]["dirty"] = True
                state_ref["value"]["can_undo"] = True
                state_ref["value"]["command_log"] = exec_commands[:]
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "command": command, "state": copy.deepcopy(state_ref["value"])}),
                )

            page.route("**/api/exec", handle_exec)

            page.goto(visual.URL, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_selector('[data-blueprint-node="branch"]', state="visible", timeout=10000)

            align_cases = [
                ("Shift+A", "align_left", lambda a, b: abs(a["x"] - b["x"]) <= 1.5),
                ("Shift+D", "align_right", lambda a, b: abs((a["x"] + a["width"]) - (b["x"] + b["width"])) <= 1.5),
                ("Shift+W", "align_top", lambda a, b: abs(a["y"] - b["y"]) <= 1.5),
                ("Shift+S", "align_bottom", lambda a, b: abs((a["y"] + a["height"]) - (b["y"] + b["height"])) <= 1.5),
                ("Alt+Shift+S", "align_center", lambda a, b: abs((a["x"] + a["width"] / 2) - (b["x"] + b["width"] / 2)) <= 1.5),
                ("Alt+Shift+W", "align_middle", lambda a, b: abs((a["y"] + a["height"] / 2) - (b["y"] + b["height"] / 2)) <= 1.5),
            ]
            for key, name, check in align_cases:
                results.extend(run_alignment_case(page, state_ref, exec_commands, key, name, check))

            state_ref["value"] = layout_state()
            exec_commands.clear()
            page.reload(wait_until="domcontentloaded", timeout=20000)
            page.wait_for_selector('[data-blueprint-node="branch"]', state="visible", timeout=10000)
            select_triple(page)
            page.keyboard.press("Alt+Shift+V")
            page.wait_for_timeout(700)
            branch_box = node_box(page, "branch")
            printer_box = node_box(page, "printer")
            text_box = node_box(page, "text")
            top_gap = printer_box["y"] - (branch_box["y"] + branch_box["height"])
            bottom_gap = text_box["y"] - (printer_box["y"] + printer_box["height"])
            page.screenshot(path=OUT / "distribute_vertical.png", full_page=True)
            results.extend([
                ("vertical distribute command replayed", any(cmd.startswith("annotate node printer Position") for cmd in exec_commands)),
                ("vertical distribute canvas gaps match", abs(top_gap - bottom_gap) <= 1.5),
            ])
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual node layout axes regression")
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
