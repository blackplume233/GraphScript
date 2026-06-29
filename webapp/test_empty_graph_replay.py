#!/usr/bin/env python3
"""E2E regression for creating a replayable graph from an empty session."""
import copy
import json
import shlex
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
URL = "http://127.0.0.1:5178/"
OUT = ROOT / "test_screenshots" / "empty_graph_replay"
OUT.mkdir(parents=True, exist_ok=True)


def empty_state():
    return {
        "file_path": "empty_replay.gs",
        "dirty": False,
        "active_graph": -1,
        "can_undo": False,
        "can_redo": False,
        "module": {
            "imports": [],
            "lets": [],
            "graphs": [],
        },
        "types": [
            {
                "type_name": "Branch",
                "is_native": True,
                "source_graph": "",
                "tags": [],
                "annotations": [],
                "pins": [
                    {"name": "enter", "kind": "exec", "direction": "in", "type": "", "annotations": []},
                    {"name": "onTrue", "kind": "exec", "direction": "out", "type": "", "annotations": []},
                    {"name": "condition", "kind": "data", "direction": "in", "type": "bool", "annotations": []},
                ],
            },
            {
                "type_name": "PrintString",
                "is_native": True,
                "source_graph": "",
                "tags": [],
                "annotations": [],
                "pins": [
                    {"name": "enter", "kind": "exec", "direction": "in", "type": "", "annotations": []},
                    {"name": "exit", "kind": "exec", "direction": "out", "type": "", "annotations": []},
                    {"name": "message", "kind": "data", "direction": "in", "type": "FString", "annotations": []},
                ],
            },
            {
                "type_name": "GetPlayerCharacter",
                "is_native": True,
                "source_graph": "",
                "tags": [],
                "annotations": [],
                "pins": [
                    {"name": "character", "kind": "data", "direction": "out", "type": "AActor", "annotations": []},
                ],
            },
        ],
        "schemas": [],
        "diagnostics": [],
        "command_log": [],
    }


def current_graph(state):
    graphs = state["module"]["graphs"]
    if not graphs:
        return None
    index = state["active_graph"]
    if index < 0 or index >= len(graphs):
        index = 0
        state["active_graph"] = 0
    return graphs[index]


def make_graph(name):
    return {
        "name": name,
        "base_type": None,
        "annotations": [],
        "parameters": [],
        "nodes": [],
        "events": [],
        "functions": [],
        "generate": None,
    }


def get_or_create_event(graph, name):
    for event in graph["events"]:
        if event["name"] == name:
            return event
    event = {"name": name, "kind": "event", "flows": [], "links": [], "annotations": []}
    graph["events"].append(event)
    return event


def split_endpoint(endpoint):
    if "." not in endpoint:
        return endpoint, ""
    return endpoint.split(".", 1)


def endpoint_text(node, pin):
    return f"{node}.{pin}" if pin else node


def parse_annotation(parts, start):
    annotation = {"name": parts[start], "args": []}
    for arg in parts[start + 1:]:
        if "=" in arg:
            name, value = arg.split("=", 1)
            annotation["args"].append({"name": name, "value": value})
        else:
            annotation["args"].append({"name": "", "value": arg})
    return annotation


def upsert_annotation(annotations, annotation):
    for index, existing in enumerate(annotations):
        if existing["name"] == annotation["name"]:
            annotations[index] = annotation
            return
    annotations.append(annotation)


def clear_empty_generate(graph):
    generate = graph.get("generate")
    if generate and not generate["comments"] and not generate["metadata"]:
        graph["generate"] = None


def move_item(items, index, direction):
    if direction == "up" and index > 0:
        items[index - 1], items[index] = items[index], items[index - 1]
        return True
    if direction == "down" and index + 1 < len(items):
        items[index + 1], items[index] = items[index], items[index + 1]
        return True
    return False


def contains_subsequence(items, expected):
    index = 0
    for item in items:
        if index < len(expected) and item == expected[index]:
            index += 1
    return index == len(expected)


def update_state_for_command(state, command):
    parts = shlex.split(command)
    if not parts:
        return

    cmd = parts[0]
    if cmd in {"create_graph", "new"} and len(parts) >= 2:
        state["module"]["graphs"].append(make_graph(parts[1]))
        state["active_graph"] = len(state["module"]["graphs"]) - 1
        return

    if cmd == "rename_graph" and len(parts) >= 3:
        for graph_item in state["module"]["graphs"]:
            if graph_item["name"] == parts[1]:
                graph_item["name"] = parts[2]
                return

    graph = current_graph(state)
    if not graph:
        return

    if cmd == "param" and len(parts) >= 4:
        direction, name, type_name = parts[1], parts[2], parts[3]
        graph["parameters"].append({
            "name": name,
            "type": type_name,
            "direction": direction,
            "default": "",
            "annotations": [],
        })
        return

    if cmd == "rename_param" and len(parts) >= 3:
        old_name, new_name = parts[1], parts[2]
        for param in graph["parameters"]:
            if param["name"] == old_name:
                param["name"] = new_name
        for event in graph["events"]:
            for link in event["links"]:
                if link["source_node"] == old_name and link["source_pin"] == "":
                    link["source_node"] = new_name
                if link["target_node"] == old_name and link["target_pin"] == "":
                    link["target_node"] = new_name
        return

    if cmd == "set_param_type" and len(parts) >= 3:
        name, type_name = parts[1], parts[2]
        for param in graph["parameters"]:
            if param["name"] == name:
                param["type"] = type_name
        return

    if cmd == "set_param_default" and len(parts) >= 2:
        name = parts[1]
        value = parts[2] if len(parts) >= 3 else ""
        for param in graph["parameters"]:
            if param["name"] == name:
                param["default"] = value
        return

    if cmd == "set_param_default_ctor" and len(parts) >= 3:
        name = parts[1]
        type_name = parts[2]
        argument = parts[3] if len(parts) >= 4 else ""
        for param in graph["parameters"]:
            if param["name"] == name:
                param["default"] = f"{type_name}({argument})"
        return

    if cmd == "set_param_default_ctor_arg" and len(parts) >= 2:
        name = parts[1]
        argument = parts[2] if len(parts) >= 3 else ""
        for param in graph["parameters"]:
            if param["name"] == name and "(" in param.get("default", "") and param["default"].endswith(")"):
                type_name = param["default"].split("(", 1)[0]
                param["default"] = f"{type_name}({argument})"
        return

    if cmd == "set_param_default_ctor_type" and len(parts) >= 3:
        name = parts[1]
        type_name = parts[2]
        for param in graph["parameters"]:
            if param["name"] == name and "(" in param.get("default", "") and param["default"].endswith(")"):
                argument = param["default"].split("(", 1)[1][:-1]
                param["default"] = f"{type_name}({argument})"
        return

    if cmd == "event" and len(parts) >= 2:
        get_or_create_event(graph, parts[1])
        return

    if cmd == "rename_event" and len(parts) >= 3:
        old_name, new_name = parts[1], parts[2]
        for event in graph["events"]:
            if event["name"] == old_name:
                event["name"] = new_name
                return

    if cmd == "fn" and len(parts) >= 2:
        if not any(fn["name"] == parts[1] for fn in graph["functions"]):
            graph["functions"].append({"name": parts[1], "kind": "function", "flows": [], "links": [], "annotations": []})
        return

    if cmd == "rename_function" and len(parts) >= 3:
        old_name, new_name = parts[1], parts[2]
        for fn in graph["functions"]:
            if fn["name"] == old_name:
                fn["name"] = new_name
                return

    if cmd in {"add", "add_node"} and len(parts) >= 3:
        graph["nodes"].append({
            "type": parts[1],
            "instance": parts[2],
            "init": "",
            "annotations": [],
            "initializer_fields": [],
        })
        return

    if cmd == "rename_node" and len(parts) >= 3:
        old_name, new_name = parts[1], parts[2]
        for node in graph["nodes"]:
            if node["instance"] == old_name:
                node["instance"] = new_name
        for event in graph["events"]:
            for flow in event["flows"]:
                if flow["from_node"] == old_name:
                    flow["from_node"] = new_name
                if flow["to_node"] == old_name:
                    flow["to_node"] = new_name
            for link in event["links"]:
                if link["target_node"] == old_name:
                    link["target_node"] = new_name
                if link["source_node"] == old_name:
                    link["source_node"] = new_name
        return

    if cmd == "flow" and len(parts) >= 3:
        source_node, source_pin = parts[1].split(".", 1)
        target_node, target_pin = parts[2].split(".", 1)
        block = get_or_create_event(graph, "BeginPlay")
        block["flows"].append({
            "from_node": source_node,
            "from_pin": source_pin,
            "to_node": target_node,
            "to_pin": target_pin,
            "annotations": [],
        })
        return

    if cmd == "link" and len(parts) >= 3:
        target_node, target_pin = split_endpoint(parts[1])
        source_node, source_pin = split_endpoint(parts[2])
        block = get_or_create_event(graph, "BeginPlay")
        block["links"].append({
            "target_node": target_node,
            "target_pin": target_pin,
            "source_node": source_node,
            "source_pin": source_pin,
            "annotations": [],
        })
        return

    if cmd == "annotate" and len(parts) >= 3 and parts[1] == "graph":
        upsert_annotation(graph["annotations"], parse_annotation(parts, 2))
        return

    if cmd == "comment" and len(parts) >= 3:
        if len(parts) >= 5 and parts[1] == "rename":
            if graph["generate"] is None:
                return
            instance = parts[2]
            old_text = parts[3]
            occurrence = int(parts[4][1:]) if len(parts) >= 6 and parts[4].startswith("#") else 0
            new_text = parts[5] if occurrence and len(parts) >= 6 else parts[4]
            seen = 0
            for comment in graph["generate"]["comments"]:
                if comment["instance"] == instance and comment["text"] == old_text:
                    seen += 1
                    if occurrence in {0, seen}:
                        comment["text"] = new_text
                        return
            return
        if len(parts) >= 5 and parts[1] == "move":
            if graph["generate"] is None:
                return
            instance = parts[2]
            direction = parts[-1]
            occurrence = int(parts[-2][1:]) if parts[-2].startswith("#") else 0
            text_parts = parts[3:-2] if occurrence else parts[3:-1]
            text = " ".join(text_parts)
            seen = 0
            for index, comment in enumerate(graph["generate"]["comments"]):
                if comment["instance"] == instance and comment["text"] == text:
                    seen += 1
                    if occurrence in {0, seen}:
                        move_item(graph["generate"]["comments"], index, direction)
                        return
            return
        if len(parts) >= 4 and parts[1] == "rm":
            if graph["generate"] is None:
                return
            instance = parts[2]
            occurrence = int(parts[-1][1:]) if parts[-1].startswith("#") else 0
            text_parts = parts[3:-1] if occurrence else parts[3:]
            text = " ".join(text_parts)
            seen = 0
            for index, comment in enumerate(graph["generate"]["comments"]):
                if comment["instance"] == instance and comment["text"] == text:
                    seen += 1
                    if occurrence in {0, seen}:
                        del graph["generate"]["comments"][index]
                        clear_empty_generate(graph)
                        return
            return
        if graph["generate"] is None:
            graph["generate"] = {"comments": [], "metadata": []}
        graph["generate"]["comments"].append({
            "instance": parts[1],
            "text": " ".join(parts[2:]),
            "annotations": [],
        })
        return

    if cmd == "meta" and len(parts) >= 3:
        if len(parts) >= 5 and parts[1] == "rename_ref":
            if graph["generate"] is None:
                return
            scope, rest = parts[2].split(":", 1)
            node, prop = rest.split(".", 1)
            value = parts[3]
            occurrence = int(parts[4][1:]) if len(parts) >= 6 and parts[4].startswith("#") else 0
            new_ref = parts[5] if occurrence and len(parts) >= 6 else parts[4]
            new_scope, new_rest = new_ref.split(":", 1)
            new_node, new_prop = new_rest.split(".", 1)
            seen = 0
            for metadata in graph["generate"]["metadata"]:
                if (
                    metadata["scope"] == scope and
                    metadata["node"] == node and
                    metadata["property"] == prop and
                    metadata["value"] == value
                ):
                    seen += 1
                    if occurrence in {0, seen}:
                        metadata["scope"] = new_scope
                        metadata["node"] = new_node
                        metadata["property"] = new_prop
                        return
            return
        if len(parts) >= 5 and parts[1] == "rename":
            if graph["generate"] is None:
                return
            scope, rest = parts[2].split(":", 1)
            node, prop = rest.split(".", 1)
            old_value = parts[3]
            occurrence = int(parts[4][1:]) if len(parts) >= 6 and parts[4].startswith("#") else 0
            new_value = parts[5] if occurrence and len(parts) >= 6 else parts[4]
            seen = 0
            for metadata in graph["generate"]["metadata"]:
                if (
                    metadata["scope"] == scope and
                    metadata["node"] == node and
                    metadata["property"] == prop and
                    metadata["value"] == old_value
                ):
                    seen += 1
                    if occurrence in {0, seen}:
                        metadata["value"] = new_value
                        return
            return
        if len(parts) >= 5 and parts[1] == "move":
            if graph["generate"] is None:
                return
            scope, rest = parts[2].split(":", 1)
            node, prop = rest.split(".", 1)
            value = parts[3]
            direction = parts[-1]
            occurrence = int(parts[4][1:]) if len(parts) >= 6 and parts[4].startswith("#") else 0
            seen = 0
            for index, metadata in enumerate(graph["generate"]["metadata"]):
                if (
                    metadata["scope"] == scope and
                    metadata["node"] == node and
                    metadata["property"] == prop and
                    metadata["value"] == value
                ):
                    seen += 1
                    if occurrence in {0, seen}:
                        move_item(graph["generate"]["metadata"], index, direction)
                        return
            return
        if len(parts) >= 4 and parts[1] == "rm":
            if graph["generate"] is None:
                return
            scope, rest = parts[2].split(":", 1)
            node, prop = rest.split(".", 1)
            value = parts[3]
            occurrence = int(parts[4][1:]) if len(parts) >= 5 and parts[4].startswith("#") else 0
            seen = 0
            for index, metadata in enumerate(graph["generate"]["metadata"]):
                if (
                    metadata["scope"] == scope and
                    metadata["node"] == node and
                    metadata["property"] == prop and
                    metadata["value"] == value
                ):
                    seen += 1
                    if occurrence in {0, seen}:
                        del graph["generate"]["metadata"][index]
                        clear_empty_generate(graph)
                        return
            return
        if graph["generate"] is None:
            graph["generate"] = {"comments": [], "metadata": []}
        scope, rest = parts[1].split(":", 1)
        node, prop = rest.split(".", 1)
        graph["generate"]["metadata"].append({
            "scope": scope,
            "node": node,
            "property": prop,
            "value": parts[2],
            "annotations": [],
        })
        return

    if cmd == "annotate" and len(parts) >= 6 and parts[1] == "generate-comment":
        if graph["generate"] is None:
            return
        instance, text = parts[2], parts[3]
        occurrence = int(parts[4][1:]) if parts[4].startswith("#") else 0
        annotation_start = 5 if occurrence else 4
        seen = 0
        for comment in graph["generate"]["comments"]:
            if comment["instance"] == instance and comment["text"] == text:
                seen += 1
                if occurrence in {0, seen}:
                    upsert_annotation(comment["annotations"], parse_annotation(parts, annotation_start))
                    return
        return

    if cmd == "annotate" and len(parts) >= 6 and parts[1] in {"generate-meta", "generate-metadata"}:
        if graph["generate"] is None:
            return
        scope, rest = parts[2].split(":", 1)
        node, prop = rest.split(".", 1)
        value = parts[3]
        occurrence = int(parts[4][1:]) if parts[4].startswith("#") else 0
        annotation_start = 5 if occurrence else 4
        seen = 0
        for metadata in graph["generate"]["metadata"]:
            if (
                metadata["scope"] == scope and
                metadata["node"] == node and
                metadata["property"] == prop and
                metadata["value"] == value
            ):
                seen += 1
                if occurrence in {0, seen}:
                    upsert_annotation(metadata["annotations"], parse_annotation(parts, annotation_start))
                    return

    if cmd == "remove_comment" and len(parts) >= 3:
        if graph["generate"] is None:
            return
        instance = parts[1]
        occurrence = int(parts[-1][1:]) if parts[-1].startswith("#") else 0
        text_parts = parts[2:-1] if occurrence else parts[2:]
        text = " ".join(text_parts)
        seen = 0
        for index, comment in enumerate(graph["generate"]["comments"]):
            if comment["instance"] == instance and comment["text"] == text:
                seen += 1
                if occurrence in {0, seen}:
                    del graph["generate"]["comments"][index]
                    clear_empty_generate(graph)
                    return
        return

    if cmd == "move_comment" and len(parts) >= 4:
        if graph["generate"] is None:
            return
        instance = parts[1]
        direction = parts[-1]
        occurrence = int(parts[-2][1:]) if parts[-2].startswith("#") else 0
        text_parts = parts[2:-2] if occurrence else parts[2:-1]
        text = " ".join(text_parts)
        seen = 0
        for index, comment in enumerate(graph["generate"]["comments"]):
            if comment["instance"] == instance and comment["text"] == text:
                seen += 1
                if occurrence in {0, seen}:
                    move_item(graph["generate"]["comments"], index, direction)
                    return
        return

    if cmd == "rename_comment" and len(parts) >= 4:
        if graph["generate"] is None:
            return
        instance = parts[1]
        old_text = parts[2]
        occurrence = int(parts[3][1:]) if len(parts) >= 5 and parts[3].startswith("#") else 0
        new_text = parts[4] if occurrence and len(parts) >= 5 else parts[3]
        seen = 0
        for comment in graph["generate"]["comments"]:
            if comment["instance"] == instance and comment["text"] == old_text:
                seen += 1
                if occurrence in {0, seen}:
                    comment["text"] = new_text
                    return
        return

    if cmd == "remove_meta" and len(parts) >= 3:
        if graph["generate"] is None:
            return
        scope, rest = parts[1].split(":", 1)
        node, prop = rest.split(".", 1)
        value = parts[2]
        occurrence = int(parts[3][1:]) if len(parts) >= 4 and parts[3].startswith("#") else 0
        seen = 0
        for index, metadata in enumerate(graph["generate"]["metadata"]):
            if (
                metadata["scope"] == scope and
                metadata["node"] == node and
                metadata["property"] == prop and
                metadata["value"] == value
            ):
                seen += 1
                if occurrence in {0, seen}:
                    del graph["generate"]["metadata"][index]
                    clear_empty_generate(graph)
                    return

    if cmd == "move_meta" and len(parts) >= 4:
        if graph["generate"] is None:
            return
        scope, rest = parts[1].split(":", 1)
        node, prop = rest.split(".", 1)
        value = parts[2]
        direction = parts[-1]
        occurrence = int(parts[3][1:]) if len(parts) >= 5 and parts[3].startswith("#") else 0
        seen = 0
        for index, metadata in enumerate(graph["generate"]["metadata"]):
            if (
                metadata["scope"] == scope and
                metadata["node"] == node and
                metadata["property"] == prop and
                metadata["value"] == value
            ):
                seen += 1
                if occurrence in {0, seen}:
                    move_item(graph["generate"]["metadata"], index, direction)
                    return

    if cmd == "rename_meta" and len(parts) >= 4:
        if graph["generate"] is None:
            return
        scope, rest = parts[1].split(":", 1)
        node, prop = rest.split(".", 1)
        old_value = parts[2]
        occurrence = int(parts[3][1:]) if len(parts) >= 5 and parts[3].startswith("#") else 0
        new_value = parts[4] if occurrence and len(parts) >= 5 else parts[3]
        seen = 0
        for metadata in graph["generate"]["metadata"]:
            if (
                metadata["scope"] == scope and
                metadata["node"] == node and
                metadata["property"] == prop and
                metadata["value"] == old_value
            ):
                seen += 1
                if occurrence in {0, seen}:
                    metadata["value"] = new_value
                    return

    if cmd == "rename_meta_ref" and len(parts) >= 4:
        if graph["generate"] is None:
            return
        scope, rest = parts[1].split(":", 1)
        node, prop = rest.split(".", 1)
        value = parts[2]
        occurrence = int(parts[3][1:]) if len(parts) >= 5 and parts[3].startswith("#") else 0
        new_ref = parts[4] if occurrence and len(parts) >= 5 else parts[3]
        new_scope, new_rest = new_ref.split(":", 1)
        new_node, new_prop = new_rest.split(".", 1)
        seen = 0
        for metadata in graph["generate"]["metadata"]:
            if (
                metadata["scope"] == scope and
                metadata["node"] == node and
                metadata["property"] == prop and
                metadata["value"] == value
            ):
                seen += 1
                if occurrence in {0, seen}:
                    metadata["scope"] = new_scope
                    metadata["node"] = new_node
                    metadata["property"] = new_prop
                    return


def _annotation_args_source(annotation):
    args = []
    for arg in annotation["args"]:
        value = json.dumps(arg["value"])
        if arg["name"]:
            args.append(f"{arg['name']} = {value}")
        else:
            args.append(value)
    return ", ".join(args)


def source_from_state(state):
    lines = []
    for graph in state["module"]["graphs"]:
        lines.append(f"Graph {graph['name']} {{")
        for param in graph["parameters"]:
            default = param.get("default", "")
            suffix = f" = {default}" if default else ""
            lines.append(f"    {param['direction']} {param['name']} : {param['type']}{suffix};")
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
                    f"        {endpoint_text(link['target_node'], link['target_pin'])} = {endpoint_text(link['source_node'], link['source_pin'])};"
                )
            lines.append("    }")
        for fn in graph["functions"]:
            lines.append(f"    fn {fn['name']} {{")
            lines.append("    }")
        if graph.get("generate"):
            lines.append("    generate {")
            for comment in graph["generate"]["comments"]:
                for annotation in comment["annotations"]:
                    lines.append(f"        [{annotation['name']}({_annotation_args_source(annotation)})]")
                lines.append(f"        Comment {comment['instance']} = \"{comment['text']}\";")
            for metadata in graph["generate"]["metadata"]:
                for annotation in metadata["annotations"]:
                    lines.append(f"        [{annotation['name']}({_annotation_args_source(annotation)})]")
                lines.append(
                    f"        {metadata['scope']}:{metadata['node']}.{metadata['property']}({metadata['value']});"
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
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5178"],
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


def run_console_command(page, command):
    prompt = page.get_by_placeholder("add_node PrintString ps1")
    prompt.fill(command)
    prompt.press("Enter")


def main():
    state = empty_state()
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

            page.goto(URL, wait_until="networkidle", timeout=20000)
            page.wait_for_selector('input[placeholder="graph"]', timeout=10000)
            initially_empty = page.locator('select option:has-text("No graphs")').count() == 1
            page.screenshot(path=OUT / "01_empty_session.png", full_page=True)

            page.get_by_role("textbox", name="graph", exact=True).fill("ReplayGraph")
            page.get_by_role("button", name="Create graph").click()
            page.wait_for_function(
                "() => Array.from(document.querySelectorAll('select option')).some(option => option.textContent === 'ReplayGraph')",
                timeout=5000,
            )
            page.get_by_label("Rename graph").fill("RenamedReplayGraph")
            page.get_by_role("button", name="Rename graph").click()
            page.wait_for_function(
                "() => Array.from(document.querySelectorAll('select option')).some(option => option.textContent === 'RenamedReplayGraph')",
                timeout=5000,
            )

            page.locator('input[placeholder="name"]').fill("speed")
            page.locator('input[placeholder="type"]').fill("float")
            page.get_by_role("button", name="Add parameter").click()
            page.wait_for_selector('[data-graph-param="speed"]', timeout=5000)
            page.get_by_label("Parameter default name for RenamedReplayGraph").fill("speed")
            page.get_by_label("Parameter default value for RenamedReplayGraph").fill("1.5")
            page.get_by_role("button", name="Set parameter default", exact=True).click()
            page.wait_for_selector('[data-graph-param-default="speed"]', timeout=5000)
            page.get_by_label("Parameter default constructor name for RenamedReplayGraph").fill("speed")
            page.get_by_label("Parameter default constructor type for RenamedReplayGraph").fill("SoftFloat")
            page.get_by_label("Parameter default constructor argument for RenamedReplayGraph").fill("2.5")
            page.get_by_role("button", name="Set parameter default constructor", exact=True).click()
            page.wait_for_function(
                "() => document.querySelector('[data-graph-param-default=\"speed\"]')?.textContent?.includes('SoftFloat(2.5)')",
                timeout=5000,
            )
            page.get_by_label("Parameter default constructor argument name for RenamedReplayGraph").fill("speed")
            page.get_by_label("Parameter default constructor argument value for RenamedReplayGraph").fill("3.5")
            page.get_by_role("button", name="Set parameter default constructor argument", exact=True).click()
            page.wait_for_function(
                "() => document.querySelector('[data-graph-param-default=\"speed\"]')?.textContent?.includes('SoftFloat(3.5)')",
                timeout=5000,
            )
            page.get_by_label("Parameter default constructor type name for RenamedReplayGraph").fill("speed")
            page.get_by_label("Parameter default constructor type value for RenamedReplayGraph").fill("PreciseFloat")
            page.get_by_role("button", name="Set parameter default constructor type", exact=True).click()
            page.wait_for_function(
                "() => document.querySelector('[data-graph-param-default=\"speed\"]')?.textContent?.includes('PreciseFloat(3.5)')",
                timeout=5000,
            )
            page.get_by_label("Parameter default constructor type row value speed").fill("RowFloat")
            page.get_by_role("button", name="Set parameter default constructor type row value speed", exact=True).click()
            page.wait_for_function(
                "() => document.querySelector('[data-graph-param-default=\"speed\"]')?.textContent?.includes('RowFloat(3.5)')",
                timeout=5000,
            )
            page.get_by_label("Parameter default constructor argument row value speed").fill("4.25")
            page.get_by_role("button", name="Set parameter default constructor argument row value speed", exact=True).click()
            page.wait_for_function(
                "() => document.querySelector('[data-graph-param-default=\"speed\"]')?.textContent?.includes('RowFloat(4.25)')",
                timeout=5000,
            )
            page.get_by_role("button", name="Clear parameter default constructor argument speed", exact=True).click()
            page.wait_for_function(
                "() => document.querySelector('[data-graph-param-default=\"speed\"]')?.textContent?.includes('RowFloat()')",
                timeout=5000,
            )
            param_default_arg_synced_after_clear = page.get_by_label("Parameter default constructor argument row value speed").input_value() == ""
            param_default_value_synced_after_clear = page.get_by_label("Parameter default row value speed").input_value() == "RowFloat()"
            page.get_by_label("Parameter default row value speed").fill("InlineDefault(seed)")
            page.get_by_role("button", name="Set parameter default row value speed", exact=True).click()
            page.wait_for_function(
                "() => document.querySelector('[data-graph-param-default=\"speed\"]')?.textContent?.includes('InlineDefault(seed)')",
                timeout=5000,
            )
            page.get_by_label("Parameter row type speed").fill("double")
            page.get_by_role("button", name="Set parameter row type speed", exact=True).click()
            page.wait_for_function(
                "() => document.querySelector('[aria-label=\"Parameter row type speed\"]')?.value === 'double'",
                timeout=5000,
            )
            param_row_type_input_synced = page.get_by_label("Parameter row type speed").input_value() == "double"

            page.locator('input[placeholder="event"]').fill("BeginPlay")
            page.get_by_role("button", name="Add event").click()
            page.wait_for_selector('[data-graph-event="BeginPlay"]', timeout=5000)

            page.locator('input[placeholder="function"]').fill("Compute")
            page.get_by_role("button", name="Add function").click()
            page.wait_for_selector('[data-graph-function="Compute"]', timeout=5000)

            page.locator('input[placeholder="Annotation"]').first.fill("Comment")
            page.locator('input[placeholder="key"]').first.fill("title")
            page.locator('input[placeholder="value"]').first.fill("Replay")
            page.get_by_role("button", name="Add annotation").first.click()
            page.wait_for_selector("text=Comment", timeout=5000)

            for command in [
                "add_node Branch branch",
                "add_node PrintString printer",
                "add_node GetPlayerCharacter player",
                "flow branch.onTrue printer.enter",
                "link printer.message player.character",
                "link printer.message speed",
                'comment printer "generated note"',
                'comment printer "later note"',
                "meta position:printer.x 120",
                "meta position:printer.y 240",
            ]:
                run_console_command(page, command)
                page.wait_for_timeout(250)

            page.wait_for_selector('[data-generate-comment-index="0"]', timeout=5000)
            comment_row = page.locator('[data-generate-comment-index="0"]')
            comment_row.locator('input[placeholder="Annotation"]').fill("Id")
            comment_row.locator('input[placeholder="value"]').fill("gen-comment-ui")
            comment_row.get_by_role("button", name="Add annotation").click()
            page.wait_for_function(
                "() => document.querySelector('[data-generate-comment-index=\"0\"]')?.textContent?.includes('Id')",
                timeout=5000,
            )

            metadata_row = page.locator('[data-generate-metadata-index="0"]')
            metadata_row.locator('input[placeholder="Annotation"]').fill("PersistentId")
            metadata_row.locator('input[placeholder="value"]').fill("gen-meta-ui")
            metadata_row.get_by_role("button", name="Add annotation").click()
            page.wait_for_function(
                "() => document.querySelector('[data-generate-metadata-index=\"0\"]')?.textContent?.includes('PersistentId')",
                timeout=5000,
            )

            page.locator('[data-generate-comment-index="0"]').get_by_label(
                "Rename generate comment printer",
            ).fill("generated note edited")
            page.locator('[data-generate-comment-index="0"]').get_by_role(
                "button",
                name="Rename generate comment printer",
            ).click()
            page.wait_for_function(
                "() => document.querySelector('[data-generate-comment-index=\"0\"]')?.textContent?.includes('generated note edited')",
                timeout=5000,
            )

            page.locator('[data-generate-metadata-index="0"]').get_by_label(
                "Rename generate metadata position:printer.x",
            ).fill("160")
            page.locator('[data-generate-metadata-index="0"]').get_by_role(
                "button",
                name="Rename generate metadata position:printer.x",
            ).click()
            page.wait_for_function(
                "() => document.querySelector('[data-generate-metadata-index=\"0\"]')?.textContent?.includes('160')",
                timeout=5000,
            )

            page.locator('[data-generate-metadata-index="0"]').get_by_label(
                "Rename generate metadata reference position:printer.x",
            ).fill("layout:printer.x")
            page.locator('[data-generate-metadata-index="0"]').get_by_role(
                "button",
                name="Rename generate metadata reference position:printer.x",
            ).click()
            page.wait_for_function(
                "() => document.querySelector('[data-generate-metadata-index=\"0\"]')?.textContent?.includes('layout:printer.x')",
                timeout=5000,
            )

            page.locator('[data-generate-comment-index="0"]').get_by_role(
                "button",
                name="Move generate comment printer down",
            ).click()
            page.wait_for_function(
                "() => document.querySelector('[data-generate-comment-index=\"1\"]')?.textContent?.includes('generated note edited')",
                timeout=5000,
            )

            page.locator('[data-generate-metadata-index="0"]').get_by_role(
                "button",
                name="Move generate metadata layout:printer.x down",
            ).click()
            page.wait_for_function(
                "() => document.querySelector('[data-generate-metadata-index=\"1\"]')?.textContent?.includes('layout:printer.x')",
                timeout=5000,
            )

            page.locator('[data-generate-comment-index="1"]').get_by_role(
                "button",
                name="Remove generate comment printer",
            ).click()
            page.wait_for_function(
                "() => document.querySelectorAll('[data-generate-comment-index]').length === 1",
                timeout=5000,
            )
            page.locator('[data-generate-comment-index="0"]').get_by_role(
                "button",
                name="Remove generate comment printer",
            ).click()
            page.wait_for_function(
                "() => document.querySelector('[data-generate-comment-index=\"0\"]') === null",
                timeout=5000,
            )
            page.locator('[data-generate-metadata-index="1"]').get_by_role(
                "button",
                name="Remove generate metadata layout:printer.x",
            ).click()
            page.wait_for_function(
                "() => document.querySelectorAll('[data-generate-metadata-index]').length === 1",
                timeout=5000,
            )
            page.locator('[data-generate-metadata-index="0"]').get_by_role(
                "button",
                name="Remove generate metadata position:printer.y",
            ).click()
            page.wait_for_function(
                "() => document.querySelector('[data-generate-metadata-index=\"0\"]') === null",
                timeout=5000,
            )

            page.wait_for_selector(".node-card:has-text('branch')", timeout=5000)
            page.wait_for_selector(".node-card:has-text('printer')", timeout=5000)
            page.get_by_label("Parameter row name speed").fill("velocity")
            page.get_by_role("button", name="Rename parameter row speed", exact=True).click()
            page.wait_for_selector('[data-graph-param="velocity"]', timeout=5000)
            param_row_rename_input_synced = page.get_by_label("Parameter row name velocity").input_value() == "velocity"
            page.get_by_role("button", name="Clear parameter default velocity").click()
            page.wait_for_function(
                "() => document.querySelector('[data-graph-param-default=\"velocity\"]') === null",
                timeout=5000,
            )
            page.get_by_label("Rename event BeginPlay").fill("Start")
            page.get_by_role("button", name="Rename event BeginPlay").click()
            page.wait_for_selector('[data-graph-event="Start"]', timeout=5000)
            page.get_by_label("Rename function Compute").fill("Calculate")
            page.get_by_role("button", name="Rename function Compute").click()
            page.wait_for_selector('[data-graph-function="Calculate"]', timeout=5000)
            page.locator(".node-card", has_text="branch").click()
            page.wait_for_selector('[data-node-instance="branch"]', timeout=5000)
            page.get_by_label("Rename node").fill("condition")
            page.get_by_role("button", name="Rename node").click()
            page.wait_for_selector(".node-card:has-text('condition')", timeout=5000)
            page.screenshot(path=OUT / "02_replay_graph_created.png", full_page=True)

            expected_sequence = [
                "create_graph ReplayGraph",
                "rename_graph ReplayGraph RenamedReplayGraph",
                "param in speed float",
                'set_param_default speed "1.5"',
                'set_param_default_ctor speed SoftFloat "2.5"',
                'set_param_default_ctor_arg speed "3.5"',
                "set_param_default_ctor_type speed PreciseFloat",
                "set_param_default_ctor_type speed RowFloat",
                'set_param_default_ctor_arg speed "4.25"',
                "set_param_default_ctor_arg speed",
                'set_param_default speed "InlineDefault(seed)"',
                "set_param_type speed double",
                "event BeginPlay",
                "fn Compute",
                'annotate graph Comment title="Replay"',
                "add_node Branch branch",
                "add_node PrintString printer",
                "add_node GetPlayerCharacter player",
                "flow branch.onTrue printer.enter",
                "link printer.message player.character",
                "link printer.message speed",
                'comment printer "generated note"',
                'comment printer "later note"',
                "meta position:printer.x 120",
                "meta position:printer.y 240",
                'annotate generate-comment printer "generated note" #1 Id "gen-comment-ui"',
                'annotate generate-meta position:printer.x "120" #1 PersistentId "gen-meta-ui"',
                'rename_comment printer "generated note" #1 "generated note edited"',
                'rename_meta position:printer.x "120" #1 "160"',
                'rename_meta_ref position:printer.x "160" #1 layout:printer.x',
                'move_comment printer "generated note edited" #1 down',
                'move_meta layout:printer.x "160" #1 down',
                'remove_comment printer "generated note edited" #1',
                'remove_comment printer "later note" #1',
                'remove_meta layout:printer.x "160" #1',
                'remove_meta position:printer.y "240" #1',
                "rename_param speed velocity",
                "set_param_default velocity",
                "rename_event BeginPlay Start",
                "rename_function Compute Calculate",
                "rename_node branch condition",
            ]
            graph = state["module"]["graphs"][0]
            event = graph["events"][0]
            results = [
                ("starts empty", initially_empty),
                ("graph renamed", graph["name"] == "RenamedReplayGraph"),
                ("command replay sequence", contains_subsequence(exec_commands, expected_sequence)),
                ("parameter default constructor tokens edited", any(command == "set_param_default_ctor_type speed PreciseFloat" for command in exec_commands)),
                ("parameter default constructor row type edited", any(command == "set_param_default_ctor_type speed RowFloat" for command in exec_commands)),
                ("parameter default constructor row argument edited", any(command == 'set_param_default_ctor_arg speed "4.25"' for command in exec_commands)),
                ("parameter default row inputs synced after clear", param_default_arg_synced_after_clear and param_default_value_synced_after_clear),
                ("parameter default constructor argument cleared", any(command == "set_param_default_ctor_arg speed" for command in exec_commands)),
                ("parameter default row value edited", any(command == 'set_param_default speed "InlineDefault(seed)"' for command in exec_commands)),
                ("parameter row type edited", any(command == "set_param_type speed double" for command in exec_commands)),
                ("parameter row type input synced", param_row_type_input_synced),
                ("parameter row rename input synced", param_row_rename_input_synced),
                ("parameter default cleared", any(param["name"] == "velocity" and param.get("default") == "" for param in graph["parameters"])),
                ("parameter renamed", any(param["name"] == "velocity" for param in graph["parameters"])),
                ("event renamed", any(event_item["name"] == "Start" for event_item in graph["events"])),
                ("function renamed", any(fn["name"] == "Calculate" for fn in graph["functions"])),
                ("annotation created", any(annotation["name"] == "Comment" for annotation in graph["annotations"])),
                ("generate items removed", graph["generate"] is None),
                ("nodes created", {node["instance"] for node in graph["nodes"]} >= {"condition", "printer", "player"}),
                ("flow created", len(event["flows"]) == 1),
                ("flow migrated after node rename", event["flows"][0]["from_node"] == "condition"),
                ("links created", len(event["links"]) == 2),
                ("bare parameter link migrated", any(link["source_node"] == "velocity" and link["source_pin"] == "" for link in event["links"])),
                ("command log visible", all(page.locator(f"text={command}").count() > 0 for command in expected_sequence)),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nEmpty graph replay regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if failed:
        print("Commands:")
        for command in exec_commands:
            print(f"  {command}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
