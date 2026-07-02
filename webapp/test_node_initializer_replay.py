#!/usr/bin/env python3
"""E2E regression for node initializer field controls replaying CLI commands."""
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
URL = "http://127.0.0.1:5183/"
OUT = ROOT / "test_screenshots" / "node_initializer_replay"
OUT.mkdir(parents=True, exist_ok=True)


def initial_state():
    return {
        "file_path": "node_initializer_replay.gs",
        "dirty": False,
        "active_graph": 0,
        "can_undo": False,
        "can_redo": False,
        "module": {
            "imports": [],
            "lets": [],
            "graphs": [{
                "name": "InitReplay",
                "base_type": None,
                "annotations": [],
                "parameters": [],
                "nodes": [{
                    "type": "PrintString",
                    "instance": "logger",
                    "init": "message = old",
                    "initializer_fields": [{"name": "message", "value": "old"}],
                    "annotations": [{"name": "Position", "args": [{"name": "X", "value": "260"}, {"name": "Y", "value": "180"}]}],
                }],
                "events": [],
                "functions": [],
            }],
        },
        "types": [{
            "type_name": "PrintString",
            "is_native": True,
            "source_graph": "",
            "tags": [],
            "annotations": [],
            "pins": [
                {"name": "enter", "kind": "exec", "direction": "in", "type": "", "annotations": []},
                {"name": "exit", "kind": "exec", "direction": "out", "type": "", "annotations": []},
                {"name": "message", "kind": "data", "direction": "in", "type": "FString", "annotations": []},
                {"name": "asset", "kind": "data", "direction": "in", "type": "FString", "annotations": []},
            ],
        }],
        "schemas": [],
        "diagnostics": [],
        "command_log": [],
    }


def set_init_field(state, node_name, field_name, value):
    graph = state["module"]["graphs"][state["active_graph"]]
    for node in graph["nodes"]:
        if node["instance"] != node_name:
            continue
        fields = node.setdefault("initializer_fields", [])
        for field in fields:
            if field["name"] == field_name:
                field["value"] = value
                break
        else:
            fields.append({"name": field_name, "value": value})
        node["init"] = ", ".join(f"{field['name']} = {field['value']}" for field in fields)
        return


def fields_from_expression(expression):
    fields = []
    if not expression or "=" not in expression:
        return fields
    for part in expression.split(","):
        if "=" not in part:
            return []
        name, value = part.split("=", 1)
        name = name.strip()
        value = value.strip()
        if not name or not value:
            return []
        fields.append({"name": name, "value": value})
    return fields


def set_init_expression(state, node_name, expression=""):
    graph = state["module"]["graphs"][state["active_graph"]]
    for node in graph["nodes"]:
        if node["instance"] != node_name:
            continue
        node["init"] = expression
        node["initializer_fields"] = fields_from_expression(expression)
        return


def set_init_constructor_field(state, node_name, field_name, constructor_type, argument=""):
    value = f"{constructor_type}({argument})"
    set_init_field(state, node_name, field_name, value)


def set_init_constructor_argument(state, node_name, field_name, argument=""):
    graph = state["module"]["graphs"][state["active_graph"]]
    for node in graph["nodes"]:
        if node["instance"] != node_name:
            continue
        fields = node.setdefault("initializer_fields", [])
        for field in fields:
            if field["name"] != field_name:
                continue
            value = field["value"]
            open_paren = value.find("(")
            if open_paren == -1 or not value.endswith(")"):
                return
            field["value"] = f"{value[:open_paren]}({argument})"
            node["init"] = ", ".join(f"{field['name']} = {field['value']}" for field in fields)
            return


def set_init_constructor_type(state, node_name, field_name, constructor_type):
    graph = state["module"]["graphs"][state["active_graph"]]
    for node in graph["nodes"]:
        if node["instance"] != node_name:
            continue
        fields = node.setdefault("initializer_fields", [])
        for field in fields:
            if field["name"] != field_name:
                continue
            value = field["value"]
            open_paren = value.find("(")
            if open_paren == -1 or not value.endswith(")"):
                return
            field["value"] = f"{constructor_type}{value[open_paren:]}"
            node["init"] = ", ".join(f"{field['name']} = {field['value']}" for field in fields)
            return


def unset_init_field(state, node_name, field_name):
    graph = state["module"]["graphs"][state["active_graph"]]
    for node in graph["nodes"]:
        if node["instance"] != node_name:
            continue
        fields = node.setdefault("initializer_fields", [])
        node["initializer_fields"] = [field for field in fields if field["name"] != field_name]
        node["init"] = ", ".join(
            f"{field['name']} = {field['value']}" for field in node["initializer_fields"]
        )
        return


def rename_init_field(state, node_name, old_name, new_name):
    graph = state["module"]["graphs"][state["active_graph"]]
    for node in graph["nodes"]:
        if node["instance"] != node_name:
            continue
        fields = node.setdefault("initializer_fields", [])
        for field in fields:
            if field["name"] == old_name:
                field["name"] = new_name
                break
        node["init"] = ", ".join(f"{field['name']} = {field['value']}" for field in fields)
        return


def source_from_state(state):
    graph = state["module"]["graphs"][0]
    lines = [f"Graph {graph['name']} {{"]
    for node in graph["nodes"]:
        lines.append(f"    {node['type']} {node['instance']}{{{node.get('init', '')}}};")
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
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5183"],
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


def stop_process_tree(proc):
    if proc.poll() is not None:
        return
    if sys.platform.startswith("win"):
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    state = initial_state()
    exec_commands = []
    proc = start_vite()
    results = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1300, "height": 820})

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
                parts = shlex.split(command)
                if len(parts) >= 2 and parts[0] == "set_init_expr":
                    set_init_expression(state, parts[1], parts[2] if len(parts) >= 3 else "")
                elif len(parts) >= 4 and parts[0] == "set_init":
                    set_init_field(state, parts[1], parts[2], parts[3])
                elif len(parts) >= 4 and parts[0] == "set_init_ctor":
                    set_init_constructor_field(
                        state,
                        parts[1],
                        parts[2],
                        parts[3],
                        parts[4] if len(parts) >= 5 else "",
                    )
                elif len(parts) >= 3 and parts[0] == "set_init_ctor_arg":
                    set_init_constructor_argument(
                        state,
                        parts[1],
                        parts[2],
                        parts[3] if len(parts) >= 4 else "",
                    )
                elif len(parts) >= 4 and parts[0] == "set_init_ctor_type":
                    set_init_constructor_type(state, parts[1], parts[2], parts[3])
                elif len(parts) >= 4 and parts[0] == "rename_init":
                    rename_init_field(state, parts[1], parts[2], parts[3])
                elif len(parts) >= 3 and parts[0] == "unset_init":
                    unset_init_field(state, parts[1], parts[2])
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
            page.wait_for_selector(".node-card:has-text('logger')", timeout=10000)
            page.locator(".node-card", has_text="logger").click()
            page.wait_for_selector('[data-node-initializer-form="logger"]', timeout=5000)

            form = page.locator('[data-node-initializer-form="logger"]')
            form.get_by_label("Initializer field for logger").fill("message")
            form.get_by_label("Initializer value for logger").fill("web-value")
            form.get_by_role("button", name="Set initializer field").click()
            page.wait_for_timeout(200)
            form.get_by_label("Initializer field for logger").fill("asset")
            form.get_by_label("Initializer value for logger").fill("payload")
            form.get_by_role("button", name="Set initializer field").click()
            page.wait_for_timeout(300)
            ctor_form = page.locator('[data-node-initializer-constructor-form="logger"]')
            ctor_form.get_by_label("Initializer constructor field for logger").fill("asset")
            ctor_form.get_by_label("Initializer constructor type for logger").fill("SoftObjectPath")
            ctor_form.get_by_label("Initializer constructor argument for logger").fill("/Game/Asset")
            ctor_form.get_by_role("button", name="Set initializer constructor").click()
            page.wait_for_timeout(300)
            ctor_arg_form = page.locator('[data-node-initializer-constructor-argument-form="logger"]')
            ctor_arg_form.get_by_label("Initializer constructor argument field for logger").fill("asset")
            ctor_arg_form.get_by_label("Initializer constructor argument value for logger").fill("/Game/OtherAsset")
            ctor_arg_form.get_by_role("button", name="Set initializer constructor argument").click()
            page.wait_for_timeout(300)
            ctor_type_form = page.locator('[data-node-initializer-constructor-type-form="logger"]')
            ctor_type_form.get_by_label("Initializer constructor type field for logger").fill("asset")
            ctor_type_form.get_by_label("Initializer constructor type value for logger").fill("AssetRef")
            ctor_type_form.get_by_role("button", name="Set initializer constructor type").click()
            page.wait_for_timeout(300)
            page.get_by_label("Initializer constructor type row value asset").fill("RowAssetRef")
            page.get_by_role("button", name="Set initializer constructor type row value asset", exact=True).click()
            page.wait_for_timeout(300)
            page.get_by_label("Initializer constructor argument row value asset").fill("/Game/RowAsset")
            page.get_by_role("button", name="Set initializer constructor argument row value asset", exact=True).click()
            page.wait_for_timeout(300)
            page.get_by_role("button", name="Clear initializer constructor argument asset", exact=True).click()
            page.wait_for_timeout(300)
            init_arg_synced_after_clear = page.get_by_label("Initializer constructor argument row value asset").input_value() == ""
            init_value_synced_after_clear = page.get_by_label("Initializer row value asset").input_value() == "RowAssetRef()"
            page.get_by_label("Initializer row value asset").fill("InlineAsset(seed)")
            page.get_by_role("button", name="Set initializer row value asset", exact=True).click()
            page.wait_for_timeout(300)
            page.get_by_label("Initializer row name message").fill("text")
            page.get_by_role("button", name="Rename initializer row field message", exact=True).click()
            page.wait_for_timeout(300)
            row_rename_input_synced = page.get_by_label("Initializer row name text").input_value() == "text"
            page.get_by_role("button", name="Remove initializer field text").click()
            page.wait_for_timeout(300)
            initializer_row_value_updated = page.locator('[data-node-init-field="asset"]', has_text="InlineAsset(seed)").count() == 1
            page.get_by_label("Initializer expression for logger").fill("Factory(seed)")
            page.get_by_role("button", name="Set initializer expression", exact=True).click()
            page.wait_for_function(
                "() => document.querySelector('[aria-label=\"Initializer expression for logger\"]')?.value === 'Factory(seed)'",
                timeout=5000,
            )
            page.wait_for_function(
                "() => document.querySelectorAll('[data-node-init-field]').length === 0",
                timeout=5000,
            )
            raw_initializer_synced = page.get_by_label("Initializer expression for logger").input_value() == "Factory(seed)"
            raw_initializer_fields_hidden = page.locator('[data-node-init-field]').count() == 0

            expected = [
                'set_init logger message "web-value"',
                'set_init logger asset "payload"',
                'set_init_ctor logger asset SoftObjectPath "/Game/Asset"',
                'set_init_ctor_arg logger asset "/Game/OtherAsset"',
                "set_init_ctor_type logger asset AssetRef",
                "set_init_ctor_type logger asset RowAssetRef",
                'set_init_ctor_arg logger asset "/Game/RowAsset"',
                "set_init_ctor_arg logger asset",
                'set_init logger asset "InlineAsset(seed)"',
                "rename_init logger message text",
                "unset_init logger text",
                'set_init_expr logger "Factory(seed)"',
            ]
            results = [
                ("initializer update command replayed", expected[0] in exec_commands),
                ("initializer append command replayed", expected[1] in exec_commands),
                ("initializer constructor command replayed", expected[2] in exec_commands),
                ("initializer constructor argument command replayed", expected[3] in exec_commands),
                ("initializer constructor type command replayed", expected[4] in exec_commands),
                ("initializer constructor row type replayed", expected[5] in exec_commands),
                ("initializer constructor row argument replayed", expected[6] in exec_commands),
                ("initializer constructor argument clear replayed", expected[7] in exec_commands),
                ("initializer row inputs synced after clear", init_arg_synced_after_clear and init_value_synced_after_clear),
                ("initializer row value command replayed", expected[8] in exec_commands),
                ("initializer row rename command replayed", expected[9] in exec_commands),
                ("initializer remove command replayed", expected[10] in exec_commands),
                ("initializer expression command replayed", expected[11] in exec_commands),
                ("command order preserved", exec_commands[:12] == expected),
                ("initializer row name input synced after rename", row_rename_input_synced),
                ("renamed initializer old field hidden", page.locator('[data-node-init-field="message"]').count() == 0),
                ("removed renamed initializer field hidden", page.locator('[data-node-init-field="text"]').count() == 0),
                ("constructor argument cleared", expected[7] in exec_commands),
                ("initializer row value updated", initializer_row_value_updated),
                ("raw initializer expression input synced", raw_initializer_synced),
                ("raw initializer hides field rows", raw_initializer_fields_hidden),
            ]
            page.screenshot(path=OUT / "node_initializer_replay.png", full_page=True)
            browser.close()
    finally:
        stop_process_tree(proc)

    failed = [name for name, ok in results if not ok]
    print("\nNode initializer replay regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        print("Commands:")
        for command in exec_commands:
            print(f"  {command}")
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
