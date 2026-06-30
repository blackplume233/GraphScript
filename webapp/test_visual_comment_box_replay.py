#!/usr/bin/env python3
"""E2E regression for Blueprint-style comment boxes replaying graph annotations."""
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

OUT = Path(__file__).parent / "test_screenshots" / "visual_comment_box_replay"
OUT.mkdir(parents=True, exist_ok=True)


def drag_comment_box(page, annotation_name, dx, dy):
    box = page.locator(f'[data-comment-box="{annotation_name}"]').first
    box.wait_for(state="visible", timeout=10000)
    rect = box.bounding_box()
    if not rect:
        raise RuntimeError("Comment box has no visible box")
    start_x = rect["x"] + 40
    start_y = rect["y"] + 24
    page.mouse.move(start_x, start_y)
    page.mouse.down()
    page.mouse.move(start_x + dx, start_y + dy, steps=16)
    page.mouse.up()


def resize_comment_box(page, annotation_name, dx, dy):
    handle = page.locator(f'[data-comment-box-resize="{annotation_name}"]').first
    handle.wait_for(state="visible", timeout=10000)
    rect = handle.bounding_box()
    if not rect:
        raise RuntimeError("Comment box resize handle has no visible box")
    start_x = rect["x"] + rect["width"] / 2
    start_y = rect["y"] + rect["height"] / 2
    page.mouse.move(start_x, start_y)
    page.mouse.down()
    page.mouse.move(start_x + dx, start_y + dy, steps=12)
    page.mouse.up()


def annotation_args(state, annotation_name):
    graph = visual.current_graph(state)
    annotation = next(item for item in graph["annotations"] if item["name"] == annotation_name)
    return {arg["name"]: arg["value"] for arg in annotation["args"]}


def node_position(state, instance_name):
    graph = visual.current_graph(state)
    node = next(item for item in graph["nodes"] if item["instance"] == instance_name)
    position = next(item for item in node["annotations"] if item["name"] == "Position")
    args = {arg["name"]: arg["value"] for arg in position["args"]}
    return {"x": int(args["X"]), "y": int(args["Y"])}


def main():
    state = visual.initial_state()
    exec_commands = []
    proc = visual.start_vite()
    results = []
    current_graph_text_after_create = ""
    current_graph_text_after_edit = ""
    current_graph_text_after_delete = ""
    created_args = {}
    edited_args = {}
    resized_args = {}
    moved_args = {}
    branch_before_move = {}
    printer_before_move = {}
    branch_after_move = {}
    printer_after_move = {}
    comment_visible_after_create = False
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
            page.screenshot(path=OUT / "01_before_comment.png", full_page=True)

            click_node(page, "branch")
            wait_node_selected(page, "branch")
            click_node(page, "printer", modifiers=["Shift"])
            wait_node_selected(page, "printer")

            page.keyboard.press("c")
            page.wait_for_selector('[data-comment-box="CommentBox_1"]', state="visible", timeout=10000)
            page.wait_for_timeout(500)
            created_args = annotation_args(state, "CommentBox_1")
            comment_visible_after_create = page.locator('[data-comment-box="CommentBox_1"]').count() == 1
            current_graph_text_after_create = page.locator('[data-current-graph-source="true"]').input_value(timeout=3000)
            page.screenshot(path=OUT / "02_after_comment_create.png", full_page=True)

            page.locator('[data-comment-box-title="CommentBox_1"]').dblclick()
            title_input = page.locator('[data-comment-box-title-input="CommentBox_1"]')
            title_input.wait_for(state="visible", timeout=10000)
            title_input.fill("Edited")
            title_input.press("Enter")
            page.wait_for_timeout(600)
            edited_args = annotation_args(state, "CommentBox_1")
            current_graph_text_after_edit = page.locator('[data-current-graph-source="true"]').input_value(timeout=3000)
            page.screenshot(path=OUT / "03_after_comment_edit.png", full_page=True)

            resize_comment_box(page, "CommentBox_1", 120, 80)
            page.wait_for_timeout(700)
            resized_args = annotation_args(state, "CommentBox_1")
            page.screenshot(path=OUT / "04_after_comment_resize.png", full_page=True)

            branch_before_move = node_position(state, "branch")
            printer_before_move = node_position(state, "printer")
            drag_comment_box(page, "CommentBox_1", 80, 60)
            page.wait_for_timeout(900)
            moved_args = annotation_args(state, "CommentBox_1")
            branch_after_move = node_position(state, "branch")
            printer_after_move = node_position(state, "printer")
            page.screenshot(path=OUT / "05_after_comment_move.png", full_page=True)

            page.locator('[data-comment-box="CommentBox_1"]').click(force=True)
            page.keyboard.press("Delete")
            page.wait_for_selector('[data-comment-box="CommentBox_1"]', state="detached", timeout=10000)
            page.wait_for_timeout(500)
            current_graph_text_after_delete = page.locator('[data-current-graph-source="true"]').input_value(timeout=3000)
            graph = visual.current_graph(state)
            mutation_commands = [cmd for cmd in exec_commands if cmd.startswith(("flow ", "link ", "unflow ", "unlink "))]
            results = [
                ("comment create command replayed", exec_commands[0].startswith("annotate graph CommentBox_1 Text=Comment")),
                ("comment annotation created", created_args.get("Text") == "Comment"),
                ("comment box rendered after create", comment_visible_after_create),
                ("current graph text had comment annotation", "[CommentBox_1(" in current_graph_text_after_create),
                ("comment title edit command replayed", any(cmd.startswith("annotate graph CommentBox_1 Text=Edited") for cmd in exec_commands)),
                ("comment title annotation updated", edited_args.get("Text") == "Edited"),
                ("current graph text had edited comment", 'Text = "Edited"' in current_graph_text_after_edit),
                ("comment resize command replayed", int(resized_args.get("W", "0")) > int(created_args.get("W", "0")) and int(resized_args.get("H", "0")) > int(created_args.get("H", "0"))),
                ("comment move command replayed", any(cmd.startswith("annotate graph CommentBox_1 Text=Edited") and cmd != exec_commands[0] for cmd in exec_commands)),
                ("comment x moved", int(moved_args.get("X", "0")) > int(created_args.get("X", "0"))),
                ("comment y moved", int(moved_args.get("Y", "0")) > int(created_args.get("Y", "0"))),
                ("wrapped branch moved with comment", branch_after_move["x"] > branch_before_move["x"] and branch_after_move["y"] > branch_before_move["y"]),
                ("wrapped printer moved with comment", printer_after_move["x"] > printer_before_move["x"] and printer_after_move["y"] > printer_before_move["y"]),
                ("wrapped node position commands replayed", any(cmd.startswith("annotate node branch Position") for cmd in exec_commands) and any(cmd.startswith("annotate node printer Position") for cmd in exec_commands)),
                ("comment delete command replayed", exec_commands[-1] == "unannotate graph CommentBox_1"),
                ("comment box removed from canvas", page.locator('[data-comment-box="CommentBox_1"]').count() == 0),
                ("comment annotation deleted", not any(item["name"] == "CommentBox_1" for item in graph["annotations"])),
                ("current graph text removed comment annotation", "CommentBox_1" not in current_graph_text_after_delete),
                ("comment box did not mutate edges", mutation_commands == []),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual comment box replay regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Created args: {created_args}")
    print(f"Edited args: {edited_args}")
    print(f"Resized args: {resized_args}")
    print(f"Moved args: {moved_args}")
    print(f"Branch moved: {branch_before_move} -> {branch_after_move}")
    print(f"Printer moved: {printer_before_move} -> {printer_after_move}")
    print(f"Screenshots: {OUT}")
    if failed:
        print("Commands:")
        for command in exec_commands:
            print(f"  {command}")
        print("Current graph text after create:")
        print(current_graph_text_after_create)
        print("Current graph text after delete:")
        print(current_graph_text_after_delete)
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
