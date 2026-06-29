#!/usr/bin/env python3
"""E2E smoke for Properties source_range navigation into the source preview."""
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
URL = "http://127.0.0.1:5178/"
OUT = ROOT / "test_screenshots" / "element_source_range_navigation"
OUT.mkdir(parents=True, exist_ok=True)

SOURCE = "\n".join([
    "Graph SourceNav : BasicSchema {",
    '    in message : FString = SoftObjectPath("hello");',
    '    [Position(X = 120, Y = 160, Asset = SoftObjectPath("Meta"))]',
    '    PrintString logger{message = "hello", count = 2, asset = SoftObjectPath("Asset")}; PrintString rawInit{SoftObjectPath("Raw")};',
    "    event OnStart {",
    "        flow logger.enter logger.exit;",
    "        link logger.value = message; link logger.value = logger.value;",
    "    }",
    "    generate {",
    '        Comment logger = "Legacy note";',
    '        position:logger.x(SoftObjectPath("Generated"));',
    "    }",
    "}",
    'let cached_path = SoftObjectPath("/Game/Cached");',
    'import "extra.d.gs";',
    "Graph PrintString {",
    "    in value : FString;",
    "    event enter {}",
    "    event exit {}",
    "}",
])


def source_range(start_line, start_column, end_line, end_column):
    return {
        "start": {"line": start_line, "column": start_column},
        "end": {"line": end_line, "column": end_column},
    }


MOCK_STATE = {
    "file_path": "source_nav.gs",
    "dirty": False,
    "active_graph": 0,
    "can_undo": False,
    "can_redo": False,
    "module": {
        "imports": [
            {
                "id": "import:extra.d.gs",
                "source_range": source_range(15, 1, 15, 21),
                "path_source_range": source_range(15, 8, 15, 20),
                "path": "extra.d.gs",
                "is_native": False,
                "loaded": True,
                "normalized_path": "extra.d.gs",
                "annotations": [],
            }
        ],
        "lets": [
            {
                "id": "let:cached_path",
                "source_range": source_range(14, 1, 14, 50),
                "name_source_range": source_range(14, 5, 14, 16),
                "type_source_range": source_range(14, 19, 14, 33),
                "constructor_source_range": source_range(14, 19, 14, 49),
                "arg_source_range": source_range(14, 34, 14, 48),
                "name": "cached_path",
                "type": "SoftObjectPath",
                "arg": "/Game/Cached",
                "annotations": [],
            }
        ],
        "graphs": [
            {
                "id": "graph:SourceNav",
                "source_range": source_range(1, 1, 13, 2),
                "name_source_range": source_range(1, 7, 1, 16),
                "name": "SourceNav",
                "base_type": "BasicSchema",
                "base_type_source_range": source_range(1, 19, 1, 30),
                "annotations": [],
                "parameters": [
                    {
                        "id": "param:SourceNav/message",
                        "source_range": source_range(2, 5, 2, 52),
                        "name_source_range": source_range(2, 8, 2, 15),
                        "type_source_range": source_range(2, 18, 2, 25),
                        "name": "message",
                        "type": "FString",
                        "direction": "in",
                        "default": 'SoftObjectPath("hello")',
                        "default_constructor_source_range": source_range(2, 28, 2, 51),
                        "default_constructor_type_source_range": source_range(2, 28, 2, 42),
                        "default_constructor_arg_source_range": source_range(2, 43, 2, 50),
                        "default_source_range": source_range(2, 28, 2, 51),
                        "annotations": [],
                    }
                ],
                "nodes": [
                    {
                        "id": "node:SourceNav/logger",
                        "source_range": source_range(4, 5, 4, 85),
                        "type_source_range": source_range(4, 5, 4, 16),
                        "instance_source_range": source_range(4, 17, 4, 23),
                        "type": "PrintString",
                        "instance": "logger",
                        "init": 'message = "hello", count = 2, asset = SoftObjectPath("Asset")',
                        "init_source_range": source_range(4, 24, 4, 85),
                        "initializer_fields": [
                            {
                                "name": "message",
                                "value": "\"hello\"",
                                "source_range": source_range(4, 24, 4, 41),
                                "name_source_range": source_range(4, 24, 4, 31),
                                "value_source_range": source_range(4, 34, 4, 41),
                            },
                            {
                                "name": "count",
                                "value": "2",
                                "source_range": source_range(4, 43, 4, 52),
                                "name_source_range": source_range(4, 43, 4, 48),
                                "value_source_range": source_range(4, 51, 4, 52),
                            },
                            {
                                "name": "asset",
                                "value": 'SoftObjectPath("Asset")',
                                "source_range": source_range(4, 54, 4, 84),
                                "name_source_range": source_range(4, 54, 4, 59),
                                "value_source_range": source_range(4, 62, 4, 85),
                                "value_constructor_source_range": source_range(4, 62, 4, 85),
                                "value_constructor_type_source_range": source_range(4, 62, 4, 76),
                                "value_constructor_arg_source_range": source_range(4, 77, 4, 84),
                            },
                        ],
                        "annotations": [
                            {
                                "name": "Position",
                                "source_range": source_range(3, 5, 3, 65),
                                "name_source_range": source_range(3, 6, 3, 14),
                                "args": [
                                    {
                                        "name": "X",
                                        "value": "120",
                                        "source_range": source_range(3, 15, 3, 22),
                                        "name_source_range": source_range(3, 15, 3, 16),
                                        "value_source_range": source_range(3, 19, 3, 22),
                                    },
                                    {
                                        "name": "Y",
                                        "value": "160",
                                        "source_range": source_range(3, 24, 3, 31),
                                        "name_source_range": source_range(3, 24, 3, 25),
                                        "value_source_range": source_range(3, 28, 3, 31),
                                    },
                                    {
                                        "name": "Asset",
                                        "value": 'SoftObjectPath("Meta")',
                                        "source_range": source_range(3, 33, 3, 63),
                                        "name_source_range": source_range(3, 33, 3, 38),
                                        "value_source_range": source_range(3, 41, 3, 63),
                                        "value_constructor_source_range": source_range(3, 41, 3, 63),
                                        "value_constructor_type_source_range": source_range(3, 41, 3, 55),
                                        "value_constructor_arg_source_range": source_range(3, 56, 3, 62),
                                    },
                                ],
                            }
                        ],
                    },
                    {
                        "id": "node:SourceNav/rawInit",
                        "source_range": source_range(4, 88, 4, 130),
                        "type_source_range": source_range(4, 88, 4, 99),
                        "instance_source_range": source_range(4, 100, 4, 107),
                        "type": "PrintString",
                        "instance": "rawInit",
                        "init": 'SoftObjectPath("Raw")',
                        "init_source_range": source_range(4, 108, 4, 129),
                        "init_constructor_source_range": source_range(4, 108, 4, 129),
                        "init_constructor_type_source_range": source_range(4, 108, 4, 122),
                        "init_constructor_arg_source_range": source_range(4, 123, 4, 128),
                        "initializer_fields": [],
                        "annotations": [],
                    }
                ],
                "events": [
                    {
                        "id": "block:SourceNav/event/OnStart",
                        "source_range": source_range(5, 5, 8, 6),
                        "name_source_range": source_range(5, 11, 5, 18),
                        "name": "OnStart",
                        "kind": "event",
                        "annotations": [],
                        "flows": [
                            {
                                "id": "flow:SourceNav/event/OnStart/logger.enter->logger.exit",
                                "source_range": source_range(6, 9, 6, 39),
                                "from_endpoint_source_range": source_range(6, 14, 6, 26),
                                "to_endpoint_source_range": source_range(6, 27, 6, 38),
                                "from_node_source_range": source_range(6, 14, 6, 20),
                                "from_pin_source_range": source_range(6, 21, 6, 26),
                                "to_node_source_range": source_range(6, 27, 6, 33),
                                "to_pin_source_range": source_range(6, 34, 6, 38),
                                "from_node": "logger",
                                "from_pin": "enter",
                                "to_node": "logger",
                                "to_pin": "exit",
                                "annotations": [],
                            }
                        ],
                        "links": [
                            {
                                "id": "link:SourceNav/event/OnStart/message->logger.value",
                                "source_range": source_range(7, 9, 7, 37),
                                "target_endpoint_source_range": source_range(7, 14, 7, 26),
                                "source_endpoint_source_range": source_range(7, 29, 7, 36),
                                "target_node_source_range": source_range(7, 14, 7, 20),
                                "target_pin_source_range": source_range(7, 21, 7, 26),
                                "source_node_source_range": source_range(7, 29, 7, 36),
                                "target_node": "logger",
                                "target_pin": "value",
                                "source_node": "message",
                                "source_pin": "",
                                "annotations": [],
                            },
                            {
                                "id": "link:SourceNav/event/OnStart/logger.value->logger.value",
                                "source_range": source_range(7, 38, 7, 71),
                                "target_endpoint_source_range": source_range(7, 43, 7, 55),
                                "source_endpoint_source_range": source_range(7, 58, 7, 70),
                                "target_node_source_range": source_range(7, 43, 7, 49),
                                "target_pin_source_range": source_range(7, 50, 7, 55),
                                "source_node_source_range": source_range(7, 58, 7, 64),
                                "source_pin_source_range": source_range(7, 65, 7, 70),
                                "target_node": "logger",
                                "target_pin": "value",
                                "source_node": "logger",
                                "source_pin": "value",
                                "annotations": [],
                            },
                        ],
                    }
                ],
                "functions": [],
                "generate": {
                    "source_range": source_range(9, 5, 12, 6),
                    "comments": [
                        {
                            "id": "generate-comment:SourceNav/logger/Legacy note",
                            "source_range": source_range(10, 9, 10, 40),
                            "instance_source_range": source_range(10, 17, 10, 23),
                            "text_source_range": source_range(10, 26, 10, 39),
                            "instance": "logger",
                            "text": "Legacy note",
                            "annotations": [],
                        }
                    ],
                    "metadata": [
                        {
                            "id": 'generate-metadata:SourceNav/position/logger/x/SoftObjectPath("Generated")',
                            "source_range": source_range(11, 9, 11, 56),
                            "scope_source_range": source_range(11, 9, 11, 17),
                            "node_source_range": source_range(11, 18, 11, 24),
                            "property_source_range": source_range(11, 25, 11, 26),
                            "value_source_range": source_range(11, 27, 11, 54),
                            "value_constructor_source_range": source_range(11, 27, 11, 54),
                            "value_constructor_type_source_range": source_range(11, 27, 11, 41),
                            "value_constructor_arg_source_range": source_range(11, 42, 11, 53),
                            "scope": "position",
                            "node": "logger",
                            "property": "x",
                            "value": 'SoftObjectPath("Generated")',
                            "annotations": [],
                        }
                    ],
                },
            }
        ],
    },
    "types": [
        {
            "type_name": "PrintString",
            "is_native": False,
            "source_graph": "PrintString",
            "source_range": source_range(16, 1, 20, 2),
            "name_source_range": source_range(16, 7, 16, 18),
            "tags": [],
            "annotations": [],
            "pins": [
                {
                    "name": "enter",
                    "kind": "exec",
                    "direction": "in",
                    "type": "",
                    "source_range": source_range(18, 5, 18, 19),
                    "name_source_range": source_range(18, 11, 18, 16),
                    "annotations": [],
                },
                {
                    "name": "exit",
                    "kind": "exec",
                    "direction": "out",
                    "type": "",
                    "source_range": source_range(19, 5, 19, 18),
                    "name_source_range": source_range(19, 11, 19, 15),
                    "annotations": [],
                },
                {
                    "name": "value",
                    "kind": "data",
                    "direction": "in",
                    "type": "FString",
                    "source_range": source_range(17, 5, 17, 24),
                    "name_source_range": source_range(17, 8, 17, 13),
                    "type_source_range": source_range(17, 16, 17, 23),
                    "annotations": [],
                },
            ],
        }
    ],
    "schemas": [],
    "diagnostics": [],
    "command_log": [],
}


def wait_for_server(proc):
    deadline = time.time() + 30
    while time.time() < deadline:
        if proc.poll() is not None:
            output = proc.stdout.read() if proc.stdout else ""
            raise RuntimeError(f"Vite server exited before ready\n{output}")
        try:
            with urllib.request.urlopen(URL, timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("Vite server did not become ready")


def stop_process_tree(proc):
    if proc.poll() is not None:
        return
    subprocess.run(
        ["taskkill", "/PID", str(proc.pid), "/T", "/F"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def start_vite():
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    proc = subprocess.Popen(
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5178", "--strictPort"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        creationflags=creationflags,
    )
    try:
        wait_for_server(proc)
    except Exception:
        stop_process_tree(proc)
        raise
    return proc


def main():
    proc = start_vite()
    results = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1400, "height": 900})
            emit_requests = []
            exec_commands = []

            page.route("**/api/state", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps(MOCK_STATE),
            ))
            page.route("**/api/emit", lambda route: (
                emit_requests.append(route.request.url),
                route.fulfill(status=200, content_type="text/plain", body=SOURCE),
            ))

            def handle_exec(route):
                payload = json.loads(route.request.post_data or "{}")
                exec_commands.append(payload.get("command", ""))
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "command": payload.get("command", ""), "state": MOCK_STATE}),
                )

            page.route("**/api/exec", handle_exec)
            page.goto(URL)
            page.wait_for_selector('[data-source-sync-state="empty"]', timeout=10000)

            page.locator('[data-source-jump="graph-SourceNav"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            graph_line_focused = page.locator('[data-source-line="1"][data-source-focused="true"]').count() == 1
            graph_range_highlighted = page.locator('[data-source-line="1"] [data-source-range="active"]').count() == 1
            source_loaded_from_emit = len(emit_requests) == 1
            page.locator('[data-source-jump="graph-name-SourceNav"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            graph_name_highlighted = page.locator('[data-source-range="active"]').inner_text() == "SourceNav"
            page.locator('[data-source-jump="graph-base-type-SourceNav"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            graph_base_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "BasicSchema"

            page.locator('[data-source-jump="import-0"]').click()
            page.wait_for_selector('[data-source-line="15"][data-source-focused="true"]', timeout=5000)
            import_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'import "extra.d.gs";'
            page.locator('[data-source-jump="import-path-0"]').click()
            page.wait_for_selector('[data-source-line="15"][data-source-focused="true"]', timeout=5000)
            import_path_highlighted = page.locator('[data-source-range="active"]').inner_text() == '"extra.d.gs"'

            page.locator('[data-source-jump="let-constructor-cached_path"]').click()
            page.wait_for_selector('[data-source-line="14"][data-source-focused="true"]', timeout=5000)
            let_constructor_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'SoftObjectPath("/Game/Cached")'
            page.locator('[data-source-jump="let-constructor-type-cached_path"]').click()
            page.wait_for_selector('[data-source-line="14"][data-source-focused="true"]', timeout=5000)
            let_constructor_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "SoftObjectPath"
            page.locator('[data-source-jump="let-constructor-arg-cached_path"]').click()
            page.wait_for_selector('[data-source-line="14"][data-source-focused="true"]', timeout=5000)
            let_constructor_arg_highlighted = page.locator('[data-source-range="active"]').inner_text() == '"/Game/Cached"'

            page.locator('[data-source-jump="param-message"]').click()
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            param_line_focused = page.locator('[data-source-line="2"][data-source-focused="true"]').count() == 1
            param_range_highlighted = page.locator('[data-source-line="2"] [data-source-range="active"]').count() == 1
            page.locator('[data-source-jump="param-name-message"]').click()
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            param_name_highlighted = page.locator('[data-source-range="active"]').inner_text() == "message"
            page.locator('[data-source-jump="param-type-message"]').click()
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            param_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "FString"
            page.locator('[data-source-jump="param-default-message"]').click()
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            param_default_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'SoftObjectPath("hello")'
            page.locator('[data-source-jump="param-default-constructor-message"]').click()
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            param_default_constructor_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'SoftObjectPath("hello")'
            page.locator('[data-source-jump="param-default-constructor-type-message"]').click()
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            param_default_constructor_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "SoftObjectPath"
            page.locator('[data-source-jump="param-default-constructor-arg-message"]').click()
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            param_default_constructor_arg_highlighted = page.locator('[data-source-range="active"]').inner_text() == '"hello"'

            page.locator('[data-source-jump="event-OnStart"]').click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            event_line_focused = page.locator('[data-source-line="5"][data-source-focused="true"]').count() == 1
            event_range_spans_block = page.locator('[data-source-line="8"][data-source-focused="true"]').count() == 1
            page.locator('[data-source-jump="event-name-OnStart"]').click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            event_name_highlighted = page.locator('[data-source-range="active"]').inner_text() == "OnStart"

            page.locator('[data-source-jump="flow-event-OnStart-logger-enter-logger-exit"]').click()
            page.wait_for_selector('[data-source-line="6"][data-source-focused="true"]', timeout=5000)
            flow_line_focused = page.locator('[data-source-line="6"][data-source-focused="true"]').count() == 1
            flow_range_highlighted = page.locator('[data-source-line="6"] [data-source-range="active"]').count() == 1
            page.locator('[data-source-jump="flow-event-OnStart-logger-enter-logger-exit-from-endpoint"]').click()
            page.wait_for_selector('[data-source-line="6"][data-source-focused="true"]', timeout=5000)
            flow_from_endpoint_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger.enter"
            page.locator('[data-source-jump="flow-event-OnStart-logger-enter-logger-exit-to-endpoint"]').click()
            page.wait_for_selector('[data-source-line="6"][data-source-focused="true"]', timeout=5000)
            flow_to_endpoint_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger.exit"
            page.locator('[data-source-jump="flow-event-OnStart-logger-enter-logger-exit-from-pin"]').click()
            page.wait_for_selector('[data-source-line="6"][data-source-focused="true"]', timeout=5000)
            flow_from_pin_highlighted = page.locator('[data-source-range="active"]').inner_text() == "enter"
            page.locator('[data-source-jump="flow-from-node-def-event-OnStart-logger"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            flow_from_node_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="flow-from-node-type-def-event-OnStart-logger"]').click()
            page.wait_for_selector('[data-source-line="16"][data-source-focused="true"]', timeout=5000)
            flow_from_node_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PrintString"
            page.locator('[data-source-jump="flow-event-OnStart-logger-enter-logger-exit-to-node"]').click()
            page.wait_for_selector('[data-source-line="6"][data-source-focused="true"]', timeout=5000)
            flow_to_node_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="flow-to-node-def-event-OnStart-logger"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            flow_to_node_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="flow-to-node-type-def-event-OnStart-logger"]').click()
            page.wait_for_selector('[data-source-line="16"][data-source-focused="true"]', timeout=5000)
            flow_to_node_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PrintString"

            page.locator('[data-source-jump="link-event-OnStart-message--logger-value"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            link_line_focused = page.locator('[data-source-line="7"][data-source-focused="true"]').count() == 1
            link_range_highlighted = page.locator('[data-source-line="7"] [data-source-range="active"]').count() == 1
            page.locator('[data-source-jump="link-event-OnStart-message--logger-value-source-endpoint"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            link_source_endpoint_highlighted = page.locator('[data-source-range="active"]').inner_text() == "message"
            page.locator('[data-source-jump="link-event-OnStart-message--logger-value-target-endpoint"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            link_target_endpoint_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger.value"
            page.locator('[data-source-jump="link-event-OnStart-message--logger-value-source-node"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            link_source_node_highlighted = page.locator('[data-source-range="active"]').inner_text() == "message"
            page.locator('[data-source-jump="link-event-OnStart-message--logger-value-source-param-def"]').click()
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            link_source_param_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "message"
            page.locator('[data-source-jump="link-event-OnStart-message--logger-value-target-node-def"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            link_target_node_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="link-event-OnStart-message--logger-value-target-node-type-def"]').click()
            page.wait_for_selector('[data-source-line="16"][data-source-focused="true"]', timeout=5000)
            link_target_node_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PrintString"
            page.locator('[data-source-jump="link-event-OnStart-message--logger-value-target-pin"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            link_target_pin_highlighted = page.locator('[data-source-range="active"]').inner_text() == "value"
            page.locator('[data-source-jump="link-event-OnStart-logger-value-logger-value"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            link_node_source_range_highlighted = page.locator('[data-source-range="active"]').inner_text() == "link logger.value = logger.value;"
            page.locator('[data-source-jump="link-event-OnStart-logger-value-logger-value-source-endpoint"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            link_node_source_endpoint_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger.value"
            page.locator('[data-source-jump="link-event-OnStart-logger-value-logger-value-source-node"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            link_node_source_node_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="link-event-OnStart-logger-value-logger-value-source-pin"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            link_node_source_pin_highlighted = page.locator('[data-source-range="active"]').inner_text() == "value"
            page.locator('[data-source-jump="link-event-OnStart-logger-value-logger-value-source-node-def"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            link_node_source_node_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="link-event-OnStart-logger-value-logger-value-source-node-type-def"]').click()
            page.wait_for_selector('[data-source-line="16"][data-source-focused="true"]', timeout=5000)
            link_node_source_node_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PrintString"
            page.locator('[data-source-jump="link-event-OnStart-logger-value-logger-value-source-pin-def"]').click()
            page.wait_for_selector('[data-source-line="17"][data-source-focused="true"]', timeout=5000)
            link_node_source_pin_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "value"
            page.locator('[data-source-jump="generate-block"]').click()
            page.wait_for_selector('[data-source-line="9"][data-source-focused="true"]', timeout=5000)
            generate_block_line_focused = page.locator('[data-source-line="9"][data-source-focused="true"]').count() == 1
            generate_block_range_spans_block = page.locator('[data-source-line="12"][data-source-focused="true"]').count() == 1
            page.locator('[data-source-jump="generate-comment-0-instance"]').click()
            page.wait_for_selector('[data-source-line="10"][data-source-focused="true"]', timeout=5000)
            generate_comment_instance_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="generate-comment-0-instance-def"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            generate_comment_instance_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="generate-comment-0-instance-type-def"]').click()
            page.wait_for_selector('[data-source-line="16"][data-source-focused="true"]', timeout=5000)
            generate_comment_instance_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PrintString"
            page.locator('[data-source-jump="generate-comment-0-text"]').click()
            page.wait_for_selector('[data-source-line="10"][data-source-focused="true"]', timeout=5000)
            generate_comment_text_highlighted = page.locator('[data-source-range="active"]').inner_text() == '"Legacy note"'
            page.locator('[data-source-jump="generate-metadata-0-scope"]').click()
            page.wait_for_selector('[data-source-line="11"][data-source-focused="true"]', timeout=5000)
            generate_metadata_scope_highlighted = page.locator('[data-source-range="active"]').inner_text() == "position"
            page.locator('[data-source-jump="generate-metadata-0-node"]').click()
            page.wait_for_selector('[data-source-line="11"][data-source-focused="true"]', timeout=5000)
            generate_metadata_node_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="generate-metadata-0-node-def"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            generate_metadata_node_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="generate-metadata-0-node-type-def"]').click()
            page.wait_for_selector('[data-source-line="16"][data-source-focused="true"]', timeout=5000)
            generate_metadata_node_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PrintString"
            page.locator('[data-source-jump="generate-metadata-0-property"]').click()
            page.wait_for_selector('[data-source-line="11"][data-source-focused="true"]', timeout=5000)
            generate_metadata_property_highlighted = page.locator('[data-source-range="active"]').inner_text() == "x"
            page.locator('[data-source-jump="generate-metadata-0-value"]').click()
            page.wait_for_selector('[data-source-line="11"][data-source-focused="true"]', timeout=5000)
            generate_metadata_value_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'SoftObjectPath("Generated")'
            page.locator('[data-source-jump="generate-metadata-0-constructor"]').click()
            page.wait_for_selector('[data-source-line="11"][data-source-focused="true"]', timeout=5000)
            generate_metadata_constructor_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'SoftObjectPath("Generated")'
            page.locator('[data-source-jump="generate-metadata-0-constructor-type"]').click()
            page.wait_for_selector('[data-source-line="11"][data-source-focused="true"]', timeout=5000)
            generate_metadata_constructor_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "SoftObjectPath"
            page.locator('[data-source-jump="generate-metadata-0-constructor-arg"]').click()
            page.wait_for_selector('[data-source-line="11"][data-source-focused="true"]', timeout=5000)
            generate_metadata_constructor_arg_highlighted = page.locator('[data-source-range="active"]').inner_text() == '"Generated"'
            source_jumps_before_node_did_not_log = exec_commands == []

            page.locator(".node-card", has_text="logger").click()
            page.wait_for_selector('[data-node-instance="logger"]', timeout=5000)
            commands_after_node_select = len(exec_commands)
            page.locator('[data-source-jump="node-logger"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            node_line_focused = page.locator('[data-source-line="4"][data-source-focused="true"]').count() == 1
            node_range_highlighted = page.locator('[data-source-line="4"] [data-source-range="active"]').count() == 1
            page.locator('[data-source-jump="node-instance-logger"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            node_instance_highlighted = page.locator('[data-source-range="active"]').inner_text() == "logger"
            page.locator('[data-source-jump="node-type-logger"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            node_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PrintString"
            page.locator('[data-source-jump="node-type-def-logger"]').click()
            page.wait_for_selector('[data-source-line="16"][data-source-focused="true"]', timeout=5000)
            node_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PrintString"
            page.locator('[data-source-jump="node-init-logger"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            node_init_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'message = "hello", count = 2, asset = SoftObjectPath("Asset")'
            page.locator('[data-source-jump="node-pin-logger-value"]').click()
            page.wait_for_selector('[data-source-line="17"][data-source-focused="true"]', timeout=5000)
            node_pin_source_highlighted = page.locator('[data-source-range="active"]').inner_text() == "in value : FString;"
            page.locator('[data-source-jump="node-pin-name-logger-enter"]').click()
            page.wait_for_selector('[data-source-line="18"][data-source-focused="true"]', timeout=5000)
            node_pin_name_highlighted = page.locator('[data-source-range="active"]').inner_text() == "enter"
            page.locator('[data-source-jump="node-pin-type-logger-value"]').click()
            page.wait_for_selector('[data-source-line="17"][data-source-focused="true"]', timeout=5000)
            node_pin_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "FString"

            page.locator('[data-source-jump="annotation-node-logger-Position"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            annotation_name_highlighted = page.locator('[data-source-range="active"]').inner_text() == "Position"
            page.locator('[data-source-jump="annotation-arg-name-node-logger-Position-X"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            annotation_arg_name_highlighted = page.locator('[data-source-range="active"]').inner_text() == "X"
            page.locator('[data-source-jump="annotation-arg-node-logger-Position-X"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            annotation_arg_highlighted = page.locator('[data-source-range="active"]').inner_text() == "X = 120"
            page.locator('[data-source-jump="annotation-arg-value-node-logger-Position-X"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            annotation_arg_value_focused = page.locator('[data-source-line="3"][data-source-focused="true"]').count() == 1
            annotation_arg_value_highlighted = page.locator('[data-source-range="active"]').inner_text() == "120"
            page.locator('[data-source-jump="annotation-arg-constructor-node-logger-Position-Asset"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            annotation_arg_constructor_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'SoftObjectPath("Meta")'
            page.locator('[data-source-jump="annotation-arg-constructor-type-node-logger-Position-Asset"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            annotation_arg_constructor_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "SoftObjectPath"
            page.locator('[data-source-jump="annotation-arg-constructor-arg-node-logger-Position-Asset"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            annotation_arg_constructor_arg_highlighted = page.locator('[data-source-range="active"]').inner_text() == '"Meta"'

            page.locator('[data-source-jump="init-field-value-node-logger-message"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            init_field_value_focused = page.locator('[data-source-line="4"][data-source-focused="true"]').count() == 1
            init_field_value_highlighted = page.locator('[data-source-range="active"]').inner_text() == '"hello"'
            page.locator('[data-source-jump="init-field-node-logger-message"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            init_field_assignment_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'message = "hello"'
            page.locator('[data-source-jump="init-field-constructor-node-logger-asset"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            init_field_constructor_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'SoftObjectPath("Asset")'
            page.locator('[data-source-jump="init-field-constructor-type-node-logger-asset"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            init_field_constructor_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "SoftObjectPath"
            page.locator('[data-source-jump="init-field-constructor-arg-node-logger-asset"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            init_field_constructor_arg_highlighted = page.locator('[data-source-range="active"]').inner_text() == '"Asset"'

            page.locator(".node-card", has_text="rawInit").click()
            page.wait_for_selector('[data-node-instance="rawInit"]', timeout=5000)
            commands_after_raw_node_select = len(exec_commands)
            page.locator('[data-source-jump="node-init-constructor-rawInit"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            raw_init_constructor_highlighted = page.locator('[data-source-range="active"]').inner_text() == 'SoftObjectPath("Raw")'
            page.locator('[data-source-jump="node-init-constructor-type-rawInit"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            raw_init_constructor_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "SoftObjectPath"
            page.locator('[data-source-jump="node-init-constructor-arg-rawInit"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            raw_init_constructor_arg_highlighted = page.locator('[data-source-range="active"]').inner_text() == '"Raw"'
            source_jumps_after_node_did_not_log = len(exec_commands) == commands_after_raw_node_select

            page.screenshot(path=OUT / "source_range_navigation.png", full_page=True)
            browser.close()

            results = [
                ("graph source line focused", graph_line_focused),
                ("graph source range highlighted", graph_range_highlighted),
                ("source loaded from emit once", source_loaded_from_emit),
                ("graph name highlighted", graph_name_highlighted),
                ("graph base type highlighted", graph_base_type_highlighted),
                ("import highlighted", import_highlighted),
                ("import path highlighted", import_path_highlighted),
                ("let constructor highlighted", let_constructor_highlighted),
                ("let constructor type highlighted", let_constructor_type_highlighted),
                ("let constructor argument highlighted", let_constructor_arg_highlighted),
                ("parameter source line focused", param_line_focused),
                ("parameter source range highlighted", param_range_highlighted),
                ("parameter name highlighted", param_name_highlighted),
                ("parameter type highlighted", param_type_highlighted),
                ("parameter default highlighted", param_default_highlighted),
                ("parameter default constructor highlighted", param_default_constructor_highlighted),
                ("parameter default constructor type highlighted", param_default_constructor_type_highlighted),
                ("parameter default constructor argument highlighted", param_default_constructor_arg_highlighted),
                ("event source line focused", event_line_focused),
                ("event source range spans block", event_range_spans_block),
                ("event name highlighted", event_name_highlighted),
                ("flow source line focused", flow_line_focused),
                ("flow source range highlighted", flow_range_highlighted),
                ("flow from endpoint highlighted", flow_from_endpoint_highlighted),
                ("flow to endpoint highlighted", flow_to_endpoint_highlighted),
                ("flow from pin highlighted", flow_from_pin_highlighted),
                ("flow from node definition highlighted", flow_from_node_definition_highlighted),
                ("flow from node type definition highlighted", flow_from_node_type_definition_highlighted),
                ("flow to node highlighted", flow_to_node_highlighted),
                ("flow to node definition highlighted", flow_to_node_definition_highlighted),
                ("flow to node type definition highlighted", flow_to_node_type_definition_highlighted),
                ("link source line focused", link_line_focused),
                ("link source range highlighted", link_range_highlighted),
                ("link source endpoint highlighted", link_source_endpoint_highlighted),
                ("link target endpoint highlighted", link_target_endpoint_highlighted),
                ("link source node highlighted", link_source_node_highlighted),
                ("link source parameter definition highlighted", link_source_param_definition_highlighted),
                ("link target node definition highlighted", link_target_node_definition_highlighted),
                ("link target node type definition highlighted", link_target_node_type_definition_highlighted),
                ("link target pin highlighted", link_target_pin_highlighted),
                ("link node source range highlighted", link_node_source_range_highlighted),
                ("link node source endpoint highlighted", link_node_source_endpoint_highlighted),
                ("link node source node highlighted", link_node_source_node_highlighted),
                ("link node source pin highlighted", link_node_source_pin_highlighted),
                ("link node source node definition highlighted", link_node_source_node_definition_highlighted),
                ("link node source node type definition highlighted", link_node_source_node_type_definition_highlighted),
                ("link node source pin definition highlighted", link_node_source_pin_definition_highlighted),
                ("generate block source line focused", generate_block_line_focused),
                ("generate block source range spans block", generate_block_range_spans_block),
                ("generate comment instance highlighted", generate_comment_instance_highlighted),
                ("generate comment instance definition highlighted", generate_comment_instance_definition_highlighted),
                ("generate comment instance type definition highlighted", generate_comment_instance_type_definition_highlighted),
                ("generate comment text highlighted", generate_comment_text_highlighted),
                ("generate metadata scope highlighted", generate_metadata_scope_highlighted),
                ("generate metadata node highlighted", generate_metadata_node_highlighted),
                ("generate metadata node definition highlighted", generate_metadata_node_definition_highlighted),
                ("generate metadata node type definition highlighted", generate_metadata_node_type_definition_highlighted),
                ("generate metadata property highlighted", generate_metadata_property_highlighted),
                ("generate metadata value highlighted", generate_metadata_value_highlighted),
                ("generate metadata constructor highlighted", generate_metadata_constructor_highlighted),
                ("generate metadata constructor type highlighted", generate_metadata_constructor_type_highlighted),
                ("generate metadata constructor argument highlighted", generate_metadata_constructor_arg_highlighted),
                ("node source line focused", node_line_focused),
                ("node source range highlighted", node_range_highlighted),
                ("node instance highlighted", node_instance_highlighted),
                ("node type highlighted", node_type_highlighted),
                ("node type definition highlighted", node_type_definition_highlighted),
                ("node initializer highlighted", node_init_highlighted),
                ("node pin source highlighted", node_pin_source_highlighted),
                ("node pin name highlighted", node_pin_name_highlighted),
                ("node pin type highlighted", node_pin_type_highlighted),
                ("annotation name highlighted", annotation_name_highlighted),
                ("annotation arg name highlighted", annotation_arg_name_highlighted),
                ("annotation arg highlighted", annotation_arg_highlighted),
                ("annotation arg value source line focused", annotation_arg_value_focused),
                ("annotation arg value highlighted", annotation_arg_value_highlighted),
                ("annotation arg constructor highlighted", annotation_arg_constructor_highlighted),
                ("annotation arg constructor type highlighted", annotation_arg_constructor_type_highlighted),
                ("annotation arg constructor argument highlighted", annotation_arg_constructor_arg_highlighted),
                ("initializer field value source line focused", init_field_value_focused),
                ("initializer field value highlighted", init_field_value_highlighted),
                ("initializer field assignment highlighted", init_field_assignment_highlighted),
                ("initializer field constructor highlighted", init_field_constructor_highlighted),
                ("initializer field constructor type highlighted", init_field_constructor_type_highlighted),
                ("initializer field constructor argument highlighted", init_field_constructor_arg_highlighted),
                ("raw initializer constructor highlighted", raw_init_constructor_highlighted),
                ("raw initializer constructor type highlighted", raw_init_constructor_type_highlighted),
                ("raw initializer constructor argument highlighted", raw_init_constructor_arg_highlighted),
                ("source navigation before node select does not log commands", source_jumps_before_node_did_not_log),
                ("source navigation after node select does not log commands", source_jumps_after_node_did_not_log),
            ]
    finally:
        stop_process_tree(proc)

    failed = [name for name, ok in results if not ok]
    print("\nElement source range navigation smoke")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
