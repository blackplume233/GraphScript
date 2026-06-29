#!/usr/bin/env python3
"""E2E regression for visual canvas edge creation replaying CLI commands."""
import copy
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

ROOT = Path(__file__).parent
URL = "http://127.0.0.1:5179/"
OUT = ROOT / "test_screenshots" / "visual_edge_replay"
OUT.mkdir(parents=True, exist_ok=True)


def initial_state():
    return {
        "file_path": "visual_edge_replay.gs",
        "dirty": False,
        "active_graph": 0,
        "can_undo": False,
        "can_redo": False,
        "module": {
            "imports": [],
            "lets": [],
            "graphs": [
                {
                    "name": "VisualReplay",
                    "base_type": None,
                    "annotations": [],
                    "parameters": [],
                    "nodes": [
                        positioned_node("Branch", "branch", 180, 140),
                        positioned_node("PrintString", "printer", 560, 140),
                        positioned_node("StringSource", "text", 180, 360),
                    ],
                    "events": [
                        {"name": "BeginPlay", "kind": "event", "annotations": [], "flows": [], "links": []},
                    ],
                    "functions": [],
                },
            ],
        },
        "types": [
            {
                "type_name": "Branch",
                "is_native": True,
                "source_graph": "",
                "tags": [],
                "pins": [
                    {"name": "enter", "kind": "exec", "direction": "in", "type": ""},
                    {"name": "onTrue", "kind": "exec", "direction": "out", "type": ""},
                    {"name": "condition", "kind": "data", "direction": "in", "type": "bool"},
                ],
            },
            {
                "type_name": "PrintString",
                "is_native": True,
                "source_graph": "",
                "tags": [],
                "pins": [
                    {"name": "enter", "kind": "exec", "direction": "in", "type": ""},
                    {"name": "exit", "kind": "exec", "direction": "out", "type": ""},
                    {"name": "message", "kind": "data", "direction": "in", "type": "FString"},
                ],
            },
            {
                "type_name": "StringSource",
                "is_native": True,
                "source_graph": "",
                "tags": [],
                "pins": [
                    {"name": "value", "kind": "data", "direction": "out", "type": "FString"},
                ],
            },
        ],
        "schemas": [],
        "diagnostics": [],
        "command_log": [],
    }


def positioned_node(type_name, instance, x, y):
    return {
        "type": type_name,
        "instance": instance,
        "init": "",
        "annotations": [
            {
                "name": "Position",
                "args": [
                    {"name": "X", "value": str(x)},
                    {"name": "Y", "value": str(y)},
                ],
            },
        ],
    }


def current_graph(state):
    return state["module"]["graphs"][state["active_graph"]]


def get_or_create_event(graph, name):
    for event in graph["events"]:
        if event["name"] == name:
            return event
    event = {"name": name, "kind": "event", "flows": [], "links": []}
    event["annotations"] = []
    graph["events"].append(event)
    return event


def unquote_token(value):
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return value[1:-1].replace('\\"', '"').replace("\\\\", "\\")
    return value


def parse_annotation(parts, start):
    if len(parts) <= start:
        return None
    annotation = {"name": parts[start], "args": []}
    for token in parts[start + 1:]:
        if "=" in token:
            name, value = token.split("=", 1)
            annotation["args"].append({"name": name, "value": unquote_token(value)})
        else:
            annotation["args"].append({"name": "", "value": unquote_token(token)})
    return annotation


def update_state_for_command(state, command):
    parts = command.split()
    if not parts:
        return

    graph = current_graph(state)
    if parts[0] == "event" and len(parts) >= 2:
        get_or_create_event(graph, parts[1])
        return

    if parts[0] == "flow" and len(parts) >= 3:
        source_node, source_pin = parts[1].split(".", 1)
        target_node, target_pin = parts[2].split(".", 1)
        event = get_or_create_event(graph, "BeginPlay")
        if not any(
            flow["from_node"] == source_node
            and flow["from_pin"] == source_pin
            and flow["to_node"] == target_node
            and flow["to_pin"] == target_pin
            for flow in event["flows"]
        ):
            event["flows"].append({
                "from_node": source_node,
                "from_pin": source_pin,
                "to_node": target_node,
                "to_pin": target_pin,
                "annotations": [],
            })
        return

    if parts[0] == "link" and len(parts) >= 3:
        target_node, target_pin = parts[1].split(".", 1)
        source_node, source_pin = parts[2].split(".", 1)
        event = get_or_create_event(graph, "BeginPlay")
        if not any(
            link["target_node"] == target_node
            and link["target_pin"] == target_pin
            and link["source_node"] == source_node
            and link["source_pin"] == source_pin
            for link in event["links"]
        ):
            event["links"].append({
                "target_node": target_node,
                "target_pin": target_pin,
                "source_node": source_node,
                "source_pin": source_pin,
                "annotations": [],
            })
        return

    if parts[0] == "unflow" and len(parts) >= 3:
        source_node, source_pin = parts[1].split(".", 1)
        target_node, target_pin = parts[2].split(".", 1)
        event = get_or_create_event(graph, "BeginPlay")
        event["flows"] = [
            flow for flow in event["flows"]
            if not (
                flow["from_node"] == source_node
                and flow["from_pin"] == source_pin
                and flow["to_node"] == target_node
                and flow["to_pin"] == target_pin
            )
        ]
        return

    if parts[0] == "unlink" and len(parts) >= 2:
        target_node, target_pin = parts[1].split(".", 1)
        event = get_or_create_event(graph, "BeginPlay")
        event["links"] = [
            link for link in event["links"]
            if not (
                link["target_node"] == target_node
                and link["target_pin"] == target_pin
            )
        ]
        return

    if parts[0] == "annotate" and len(parts) >= 7 and parts[1] == "flow":
        _, _, block_kind, block_name, source_ref, target_ref, *_ = parts
        source_node, source_pin = source_ref.split(".", 1)
        target_node, target_pin = target_ref.split(".", 1)
        annotation = parse_annotation(parts, 6)
        if annotation is None:
            return
        event = get_or_create_event(graph, block_name if block_kind == "event" else "BeginPlay")
        for flow in event["flows"]:
            if (
                flow["from_node"] == source_node
                and flow["from_pin"] == source_pin
                and flow["to_node"] == target_node
                and flow["to_pin"] == target_pin
            ):
                flow.setdefault("annotations", [])
                flow["annotations"] = [
                    existing for existing in flow["annotations"]
                    if existing["name"] != annotation["name"]
                ]
                flow["annotations"].append(annotation)
                return

    if parts[0] == "annotate" and len(parts) >= 7 and parts[1] == "link":
        _, _, block_kind, block_name, target_ref, source_ref, *_ = parts
        target_node, target_pin = target_ref.split(".", 1)
        source_parts = source_ref.split(".", 1)
        source_node = source_parts[0]
        source_pin = source_parts[1] if len(source_parts) > 1 else ""
        annotation = parse_annotation(parts, 6)
        if annotation is None:
            return
        event = get_or_create_event(graph, block_name if block_kind == "event" else "BeginPlay")
        for link in event["links"]:
            if link["target_node"] == target_node and link["target_pin"] == target_pin:
                link["source_node"] = source_node
                link["source_pin"] = source_pin
                link.setdefault("annotations", [])
                link["annotations"] = [
                    existing for existing in link["annotations"]
                    if existing["name"] != annotation["name"]
                ]
                link["annotations"].append(annotation)
                return


def source_from_state(state):
    lines = []
    for graph in state["module"]["graphs"]:
        lines.append(f"Graph {graph['name']} {{")
        for node in graph["nodes"]:
            lines.append(f"    {node['type']} {node['instance']}{{}};")
        for event in graph["events"]:
            lines.append(f"    event {event['name']} {{")
            for flow in event["flows"]:
                lines.append(
                    f"        flow {flow['from_node']}.{flow['from_pin']} -> {flow['to_node']}.{flow['to_pin']};"
                )
            for link in event["links"]:
                lines.append(
                    f"        {link['target_node']}.{link['target_pin']} = {link['source_node']}.{link['source_pin']};"
                )
            lines.append("    }")
        lines.append("}")
    return "\n".join(lines)


def wait_for_server():
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(URL, timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("Vite server did not become ready")


def start_vite():
    proc = subprocess.Popen(
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5179", "--strictPort"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        wait_for_server()
    except Exception:
        proc.terminate()
        raise
    return proc


def drag_port(page, source_node, source_port, target_node, target_port):
    source = page.locator(
        f'[data-blueprint-node="{source_node}"] [data-port-id="{source_port}"][data-testid="sdk.workflow.canvas.node.port"]'
    ).first
    target = page.locator(
        f'[data-blueprint-node="{target_node}"] [data-port-id="{target_port}"][data-testid="sdk.workflow.canvas.node.port"]'
    ).first
    source.wait_for(state="visible", timeout=10000)
    target.wait_for(state="visible", timeout=10000)
    source_box = source.bounding_box()
    target_box = target.bounding_box()
    source_x = source_box["x"] + source_box["width"] / 2
    source_y = source_box["y"] + source_box["height"] / 2
    target_x = target_box["x"] + target_box["width"] / 2
    target_y = target_box["y"] + target_box["height"] / 2
    page.mouse.move(source_x, source_y)
    page.mouse.down()
    page.wait_for_timeout(150)
    page.mouse.move(source_x + 40, source_y, steps=8)
    page.mouse.move((source_x + target_x) / 2, (source_y + target_y) / 2, steps=15)
    page.mouse.move(target_x, target_y, steps=15)
    page.wait_for_timeout(100)
    page.mouse.up()


def main():
    state = initial_state()
    exec_commands = []
    proc = start_vite()
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
                body=source_from_state(state),
            ))

            def handle_exec(route):
                payload = json.loads(route.request.post_data or "{}")
                command = payload.get("command", "")
                exec_commands.append(command)
                update_state_for_command(state, command)
                state["dirty"] = True
                state["can_undo"] = True
                state["command_log"] = exec_commands[:]
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "command": command, "state": copy.deepcopy(state)}),
                )

            page.route("**/api/exec", handle_exec)

            page.goto(URL, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_selector(".node-card:has-text('branch')", timeout=10000)
            page.locator('select[title="Active event/function for visual edge edits"]').select_option("event:BeginPlay")
            page.wait_for_timeout(500)
            page.screenshot(path=OUT / "01_before_edges.png", full_page=True)

            drag_port(page, "branch", "exec-out-onTrue", "printer", "exec-in-enter")
            page.wait_for_timeout(1200)
            drag_port(page, "text", "data-out-value", "printer", "data-in-message")
            page.wait_for_timeout(1200)
            page.screenshot(path=OUT / "02_after_visual_edges.png", full_page=True)

            graph = current_graph(state)
            event = graph["events"][0]
            expected = [
                "event BeginPlay",
                "event BeginPlay",
                "flow branch.onTrue printer.enter",
                "event BeginPlay",
                "link printer.message text.value",
            ]
            results = [
                ("active block selected", exec_commands[:1] == ["event BeginPlay"]),
                ("visual flow command replayed", "flow branch.onTrue printer.enter" in exec_commands),
                ("visual link command replayed", "link printer.message text.value" in exec_commands),
                ("command order preserved", exec_commands[:len(expected)] == expected),
                ("state has flow", len(event["flows"]) == 1),
                ("state has link", len(event["links"]) == 1),
                ("command log visible", all(page.locator(f"text={command}").count() > 0 for command in expected)),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual edge replay regression")
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
