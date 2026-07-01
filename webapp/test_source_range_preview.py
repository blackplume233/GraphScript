#!/usr/bin/env python3
"""E2E regression for source diagnostic range preview and focus."""
import json
import re
import base64
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
URL = "http://127.0.0.1:5177/"
OUT = ROOT / "test_screenshots" / "source_range_preview"
OUT.mkdir(parents=True, exist_ok=True)

SOURCE = "\n".join([
    "Graph SourceDiag {",
    "    in speed float;",
    "    PrintString logger{};",
    "}",
])

FIXED_SOURCE = SOURCE.replace("in speed float;", "in speed : float;")
MANUAL_SOURCE = FIXED_SOURCE.replace("logger", "writer")
MANUAL_INVALID_SOURCE = MANUAL_SOURCE.replace("in speed : float;", "in speed float;")
MANUAL_IMPORT_SOURCE = 'import native "Core";\n' + MANUAL_SOURCE
BLOCKED_IMPORT_SOURCE = 'import "../blocked.d.gs";\n' + MANUAL_SOURCE
DISCONNECTED_RESOLVER_IMPORT_SOURCE = 'import "resolver-disconnect.d.gs";\n' + MANUAL_SOURCE
MULTI_STATUS_IMPORT_SOURCE = 'import "a.d.gs";\nimport "depth-root.d.gs";\n' + MANUAL_SOURCE
RESOLVED_IMPORT_SOURCE = 'import "custom.d.gs";\n' + MANUAL_SOURCE
RESOLVED_IMPORT_EDITED_SOURCE = RESOLVED_IMPORT_SOURCE.replace("writer", "writer_after_import")
FAILING_REPLAY_IMPORT_SOURCE = 'import "failing-parent.d.gs";\n' + MANUAL_SOURCE
SLOW_REPLAY_IMPORT_SOURCE = 'import "slow.d.gs";\n' + MANUAL_SOURCE
DISCONNECTED_REPLAY_IMPORT_SOURCE = 'import "disconnected.d.gs";\n' + MANUAL_SOURCE
STALE_SOURCE = FIXED_SOURCE + "\n\nGraph BackendChanged {\n    PrintString backend{};\n}"


def source_hash(text):
    value = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


SOURCE_HASH = source_hash(SOURCE)
MANUAL_PATCH_REPLACEMENT = ": float;\n    PrintString writ"
SOURCE_PATCH_COMMAND = (
    "apply_source_patch 2 14 2 14 " +
    base64.b64encode(": ".encode("utf-8")).decode("ascii") +
    " " + SOURCE_HASH
)
MANUAL_PATCH_COMMAND = (
    "apply_source_patch 2 14 3 21 " +
    base64.b64encode(MANUAL_PATCH_REPLACEMENT.encode("utf-8")).decode("ascii") +
    " " + SOURCE_HASH
)
MANUAL_SOURCE_COMMAND = "apply_source_b64 " + base64.b64encode(MANUAL_SOURCE.encode("utf-8")).decode("ascii")


def empty_target():
    return {
        "graph": "",
        "block_kind": "",
        "block_name": "",
        "node_instance": "",
        "pin_name": "",
        "parameter_name": "",
        "reference": "",
        "connection_kind": "",
    }


SOURCE_DIAGNOSTIC = {
    "severity": "error",
    "message": "Expected target node",
    "context": "",
    "code": "GS_TEST_SOURCE_RANGE",
    "range": {
        "start": {"line": 2, "column": 14},
        "end": {"line": 2, "column": 19},
    },
    "hint": "Check this source range.",
    "target": empty_target(),
    "actions": [
        {
            "title": "Create graph from diagnostic",
            "kind": "quickfix",
            "command": "create_graph FixedByAction",
            "edit_range": {
                "start": {"line": 1, "column": 1},
                "end": {"line": 1, "column": 1},
            },
            "replacement": "",
        },
        {
            "title": "Insert ':'",
            "kind": "quickfix.source.insert",
            "command": "",
            "edit_range": {
                "start": {"line": 2, "column": 14},
                "end": {"line": 2, "column": 14},
            },
            "replacement": ": ",
        }
    ],
}

BLOCKED_IMPORT_DIAGNOSTIC = {
    "severity": "error",
    "message": "Blocked source import outside root '../blocked.d.gs'",
    "context": "../blocked.d.gs",
    "code": "GS_IMPORT_PATH_BLOCKED",
    "range": {
        "start": {"line": 1, "column": 1},
        "end": {"line": 1, "column": 25},
    },
    "hint": "Keep source imports inside the configured base_dir.",
    "target": empty_target(),
    "actions": [],
}

BLOCKED_IMPORT_ENVIRONMENT = {
    "mode": "resolved",
    "declarations": [
        {
            "path": "../blocked.d.gs",
            "normalized_path": "",
            "content_hash": "",
            "status": "blocked",
            "message": "Import path escapes the configured source root",
            "command": "",
            "parent_path": "",
            "parent_normalized_path": "",
            "import_chain": "source -> ../blocked.d.gs",
            "depth": 1,
        }
    ],
    "environment_hash": "resolverdeadbeef",
    "type_count": 0,
    "node_type_count": 1,
    "schema_count": 0,
    "limits": {
        "max_imports": 32,
        "max_import_depth": 8,
        "max_file_bytes": 1048576,
        "max_total_bytes": 4194304,
    },
}

MULTI_STATUS_IMPORT_DIAGNOSTICS = [
    {
        "severity": "error",
        "message": "Import cycle detected: source -> a.d.gs -> b.d.gs -> a.d.gs",
        "context": "a.d.gs",
        "code": "GS_IMPORT_CYCLE",
        "range": {
            "start": {"line": 1, "column": 1},
            "end": {"line": 1, "column": 17},
        },
        "hint": "Remove one import from the cycle.",
        "target": empty_target(),
        "actions": [],
    },
    {
        "severity": "error",
        "message": "Maximum import depth exceeded at depth-child.d.gs",
        "context": "depth-child.d.gs",
        "code": "GS_IMPORT_DEPTH_EXCEEDED",
        "range": {
            "start": {"line": 2, "column": 1},
            "end": {"line": 2, "column": 26},
        },
        "hint": "Reduce nested declaration imports or raise the resolver depth limit.",
        "target": empty_target(),
        "actions": [],
    },
]

MULTI_STATUS_IMPORT_ENVIRONMENT = {
    "mode": "resolved",
    "declarations": [
        {
            "path": "a.d.gs",
            "normalized_path": "a.d.gs",
            "content_hash": "",
            "status": "dependency_error",
            "message": "Dependency failed while resolving b.d.gs",
            "command": "",
            "parent_path": "",
            "parent_normalized_path": "",
            "import_chain": "source -> a.d.gs",
            "depth": 1,
        },
        {
            "path": "b.d.gs",
            "normalized_path": "b.d.gs",
            "content_hash": "",
            "status": "dependency_error",
            "message": "Dependency failed while resolving a.d.gs",
            "command": "",
            "parent_path": "a.d.gs",
            "parent_normalized_path": "a.d.gs",
            "import_chain": "source -> a.d.gs -> b.d.gs",
            "depth": 2,
        },
        {
            "path": "a.d.gs",
            "normalized_path": "a.d.gs",
            "content_hash": "",
            "status": "cycle",
            "message": "Import cycle detected",
            "command": "",
            "parent_path": "b.d.gs",
            "parent_normalized_path": "b.d.gs",
            "import_chain": "source -> a.d.gs -> b.d.gs -> a.d.gs",
            "depth": 3,
        },
        {
            "path": "depth-root.d.gs",
            "normalized_path": "depth-root.d.gs",
            "content_hash": "",
            "status": "dependency_error",
            "message": "Dependency failed while resolving depth-child.d.gs",
            "command": "",
            "parent_path": "",
            "parent_normalized_path": "",
            "import_chain": "source -> depth-root.d.gs",
            "depth": 1,
        },
        {
            "path": "depth-child.d.gs",
            "normalized_path": "depth-child.d.gs",
            "content_hash": "",
            "status": "too_deep",
            "message": "Maximum import depth exceeded",
            "command": "",
            "parent_path": "depth-root.d.gs",
            "parent_normalized_path": "depth-root.d.gs",
            "import_chain": "source -> depth-root.d.gs -> depth-child.d.gs",
            "depth": 2,
        },
    ],
    "environment_hash": "resolver-multi-status",
    "type_count": 0,
    "node_type_count": 1,
    "schema_count": 0,
    "limits": {
        "max_imports": 32,
        "max_import_depth": 1,
        "max_file_bytes": 1048576,
        "max_total_bytes": 4194304,
    },
}

RESOLVED_IMPORT_ENVIRONMENT_A = {
    "mode": "resolved",
    "declarations": [
        {
            "path": "nested.d.gs",
            "normalized_path": "nested.d.gs",
            "content_hash": "nested-declaration-a",
            "status": "loaded",
            "message": "Loaded nested declaration into dry-run environment",
            "command": "import nested.d.gs",
            "parent_path": "custom.d.gs",
            "parent_normalized_path": "custom.d.gs",
            "import_chain": "source -> custom.d.gs -> nested.d.gs",
            "depth": 2,
        },
        {
            "path": "custom.d.gs",
            "normalized_path": "custom.d.gs",
            "content_hash": "declaration-a",
            "status": "loaded",
            "message": "Loaded declaration into dry-run environment",
            "command": "import custom.d.gs",
            "parent_path": "",
            "parent_normalized_path": "",
            "import_chain": "source -> custom.d.gs",
            "depth": 1,
        }
    ],
    "environment_hash": "resolver-env-a",
    "type_count": 1,
    "node_type_count": 2,
    "schema_count": 0,
    "limits": {
        "max_imports": 32,
        "max_import_depth": 8,
        "max_file_bytes": 1048576,
        "max_total_bytes": 4194304,
    },
}

RESOLVED_IMPORT_ENVIRONMENT_B = {
    **RESOLVED_IMPORT_ENVIRONMENT_A,
    "environment_hash": "resolver-env-b",
    "declarations": [
        RESOLVED_IMPORT_ENVIRONMENT_A["declarations"][0],
        {
            **RESOLVED_IMPORT_ENVIRONMENT_A["declarations"][1],
            "content_hash": "declaration-b",
            "command": "import custom.d.gs",
            "import_chain": "source -> custom.d.gs",
        },
    ],
}

FAILING_REPLAY_IMPORT_ENVIRONMENT = {
    "mode": "resolved",
    "declarations": [
        {
            "path": "failing-child.d.gs",
            "normalized_path": "failing-child.d.gs",
            "content_hash": "failing-child",
            "status": "loaded",
            "message": "Loaded child declaration into dry-run environment",
            "command": "import failing-child.d.gs",
            "parent_path": "failing-parent.d.gs",
            "parent_normalized_path": "failing-parent.d.gs",
            "import_chain": "source -> failing-parent.d.gs -> failing-child.d.gs",
            "depth": 2,
        },
        {
            "path": "failing-parent.d.gs",
            "normalized_path": "failing-parent.d.gs",
            "content_hash": "failing-parent",
            "status": "loaded",
            "message": "Loaded parent declaration into dry-run environment",
            "command": "import failing-parent.d.gs",
            "parent_path": "",
            "parent_normalized_path": "",
            "import_chain": "source -> failing-parent.d.gs",
            "depth": 1,
        },
        {
            "path": "after-failure.d.gs",
            "normalized_path": "after-failure.d.gs",
            "content_hash": "after-failure",
            "status": "loaded",
            "message": "Loaded independent declaration into dry-run environment",
            "command": "import after-failure.d.gs",
            "parent_path": "",
            "parent_normalized_path": "",
            "import_chain": "source -> after-failure.d.gs",
            "depth": 1,
        },
    ],
    "environment_hash": "resolver-failing-replay",
    "type_count": 1,
    "node_type_count": 3,
    "schema_count": 0,
    "limits": {
        "max_imports": 32,
        "max_import_depth": 8,
        "max_file_bytes": 1048576,
        "max_total_bytes": 4194304,
    },
}

SLOW_REPLAY_IMPORT_ENVIRONMENT = {
    "mode": "resolved",
    "declarations": [
        {
            "path": "slow.d.gs",
            "normalized_path": "slow.d.gs",
            "content_hash": "slow",
            "status": "loaded",
            "message": "Loaded slow declaration into dry-run environment",
            "command": "import slow.d.gs",
            "parent_path": "",
            "parent_normalized_path": "",
            "import_chain": "source -> slow.d.gs",
            "depth": 1,
        },
    ],
    "environment_hash": "resolver-slow-replay",
    "type_count": 1,
    "node_type_count": 2,
    "schema_count": 0,
    "limits": {
        "max_imports": 32,
        "max_import_depth": 8,
        "max_file_bytes": 1048576,
        "max_total_bytes": 4194304,
    },
}

DISCONNECTED_REPLAY_IMPORT_ENVIRONMENT = {
    "mode": "resolved",
    "declarations": [
        {
            "path": "disconnected.d.gs",
            "normalized_path": "disconnected.d.gs",
            "content_hash": "disconnected",
            "status": "loaded",
            "message": "Loaded disconnected declaration into dry-run environment",
            "command": "import disconnected.d.gs",
            "parent_path": "",
            "parent_normalized_path": "",
            "import_chain": "source -> disconnected.d.gs",
            "depth": 1,
        },
        {
            "path": "after-disconnect.d.gs",
            "normalized_path": "after-disconnect.d.gs",
            "content_hash": "after-disconnect",
            "status": "loaded",
            "message": "Loaded later declaration into dry-run environment",
            "command": "import after-disconnect.d.gs",
            "parent_path": "",
            "parent_normalized_path": "",
            "import_chain": "source -> after-disconnect.d.gs",
            "depth": 1,
        },
    ],
    "environment_hash": "resolver-disconnected-replay",
    "type_count": 1,
    "node_type_count": 3,
    "schema_count": 0,
    "limits": {
        "max_imports": 32,
        "max_import_depth": 8,
        "max_file_bytes": 1048576,
        "max_total_bytes": 4194304,
    },
}

MOCK_STATE = {
    "file_path": "source_diag.gs",
    "dirty": False,
    "active_graph": 0,
    "can_undo": False,
    "can_redo": False,
    "module": {
        "imports": [],
        "lets": [],
        "graphs": [
            {
                "name": "SourceDiag",
                "base_type": None,
                "annotations": [],
                "parameters": [],
                "nodes": [{"type": "PrintString", "instance": "logger", "init": "", "annotations": []}],
                "events": [{"name": "BeginPlay", "kind": "event", "flows": [], "links": []}],
                "functions": [],
            }
        ],
    },
    "types": [
        {
            "type_name": "PrintString",
            "is_native": True,
            "source_graph": "",
            "tags": [],
            "pins": [
                {"name": "enter", "kind": "exec", "direction": "in", "type": ""},
                {"name": "exit", "kind": "exec", "direction": "out", "type": ""},
            ],
        }
    ],
    "schemas": [],
    "diagnostics": [],
    "command_log": [],
}


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
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5177"],
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


def activate_workbench_tab(page, title):
    page.locator(".dv-tab").filter(has_text=title).first.click(timeout=5000)


def set_source_editor(page, text):
    page.wait_for_selector('[data-source-editor="true"]', timeout=10000)
    page.wait_for_function("() => window.__graphScriptSourceEditor", timeout=15000)
    page.evaluate("(value) => window.__graphScriptSourceEditor.setValue(value)", text)


def source_editor_value(page):
    page.wait_for_selector('[data-source-editor="true"]', timeout=10000)
    page.wait_for_function("() => window.__graphScriptSourceEditor", timeout=15000)
    return page.evaluate("() => window.__graphScriptSourceEditor.getValue()")


def source_editor_selection(page):
    page.wait_for_selector('[data-source-editor="true"]', timeout=10000)
    page.wait_for_function("() => window.__graphScriptSourceEditor", timeout=15000)
    return page.evaluate("() => window.__graphScriptSourceEditor.getSelectedText()")


def ensure_source_editing(page):
    activate_workbench_tab(page, "SOURCE")
    if page.get_by_title("Edit source").count() > 0:
        page.get_by_title("Edit source").click()
    page.wait_for_selector('[data-source-editor="true"]', timeout=10000)


def main():
    proc = start_vite()
    results = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1500, "height": 950})
            exec_commands = []

            page.route("**/api/state", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps(MOCK_STATE),
            ))
            emitted_source = {"text": SOURCE}
            page.route("**/api/emit", lambda route: route.fulfill(
                status=200,
                content_type="text/plain",
                body=emitted_source["text"],
            ))
            diagnostic_requests = []
            resolver_requests = []
            resolved_import_diagnostics_count = {"value": 0}
            source_patch_requests = []
            source_apply_requests = []

            def handle_diagnostics(route):
                body = route.request.post_data or ""
                payload = None
                request_source = body
                if body.startswith("{"):
                    payload = json.loads(body)
                    request_source = payload.get("source", "")
                    if payload.get("resolve_imports"):
                        resolver_requests.append(payload)
                diagnostic_requests.append(request_source)
                if request_source == BLOCKED_IMPORT_SOURCE:
                    route.fulfill(
                        status=200,
                        content_type="application/json",
                        body=json.dumps({
                            "ok": False,
                            "stage": "resolver",
                            "diagnostics": [BLOCKED_IMPORT_DIAGNOSTIC],
                            "environment": BLOCKED_IMPORT_ENVIRONMENT,
                        }),
                    )
                    return
                if request_source == DISCONNECTED_RESOLVER_IMPORT_SOURCE:
                    route.abort("failed")
                    return
                if request_source == MULTI_STATUS_IMPORT_SOURCE:
                    route.fulfill(
                        status=200,
                        content_type="application/json",
                        body=json.dumps({
                            "ok": False,
                            "stage": "resolver",
                            "diagnostics": MULTI_STATUS_IMPORT_DIAGNOSTICS,
                            "environment": MULTI_STATUS_IMPORT_ENVIRONMENT,
                        }),
                    )
                    return
                if request_source in (RESOLVED_IMPORT_SOURCE, RESOLVED_IMPORT_EDITED_SOURCE):
                    resolved_import_diagnostics_count["value"] += 1
                    environment = RESOLVED_IMPORT_ENVIRONMENT_A if request_source == RESOLVED_IMPORT_SOURCE and resolved_import_diagnostics_count["value"] == 1 else RESOLVED_IMPORT_ENVIRONMENT_B
                    route.fulfill(
                        status=200,
                        content_type="application/json",
                        body=json.dumps({
                            "ok": True,
                            "stage": "asset",
                            "diagnostics": [],
                            "environment": environment,
                        }),
                    )
                    return
                if request_source == FAILING_REPLAY_IMPORT_SOURCE:
                    route.fulfill(
                        status=200,
                        content_type="application/json",
                        body=json.dumps({
                            "ok": True,
                            "stage": "asset",
                            "diagnostics": [],
                            "environment": FAILING_REPLAY_IMPORT_ENVIRONMENT,
                        }),
                    )
                    return
                if request_source == SLOW_REPLAY_IMPORT_SOURCE:
                    route.fulfill(
                        status=200,
                        content_type="application/json",
                        body=json.dumps({
                            "ok": True,
                            "stage": "asset",
                            "diagnostics": [],
                            "environment": SLOW_REPLAY_IMPORT_ENVIRONMENT,
                        }),
                    )
                    return
                if request_source == DISCONNECTED_REPLAY_IMPORT_SOURCE:
                    route.fulfill(
                        status=200,
                        content_type="application/json",
                        body=json.dumps({
                            "ok": True,
                            "stage": "asset",
                            "diagnostics": [],
                            "environment": DISCONNECTED_REPLAY_IMPORT_ENVIRONMENT,
                        }),
                    )
                    return
                ok = request_source in (FIXED_SOURCE, MANUAL_SOURCE)
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({
                        "ok": ok,
                        "stage": "asset" if ok else "parser",
                        "diagnostics": [] if ok else [SOURCE_DIAGNOSTIC],
                    }),
                )

            page.route("**/api/diagnostics", handle_diagnostics)

            def handle_source_patch(route):
                body = route.request.post_data or ""
                source_patch_requests.append(body)
                payload = json.loads(body or "{}")
                quickfix_patch_ok = (
                    payload.get("start_line") == 2 and
                    payload.get("start_column") == 14 and
                    payload.get("end_line") == 2 and
                    payload.get("end_column") == 14 and
                    payload.get("replacement") == ": " and
                    payload.get("base_hash") == SOURCE_HASH and
                    payload.get("fallback_source") == FIXED_SOURCE
                )
                manual_patch_ok = (
                    payload.get("start_line") == 2 and
                    payload.get("start_column") == 14 and
                    payload.get("end_line") == 3 and
                    payload.get("end_column") == 21 and
                    payload.get("replacement") == MANUAL_PATCH_REPLACEMENT and
                    payload.get("base_hash") == SOURCE_HASH and
                    payload.get("fallback_source") == MANUAL_SOURCE
                )
                resolved_import_patch_ok = (
                    payload.get("fallback_source") == RESOLVED_IMPORT_SOURCE and
                    payload.get("environment_hash") == "resolver-env-a" and
                    payload.get("resolve_imports") is True and
                    payload.get("source_path") == "source_diag.gs"
                )
                post_replay_import_patch_ok = (
                    payload.get("fallback_source") == RESOLVED_IMPORT_EDITED_SOURCE and
                    payload.get("base_hash") == source_hash(RESOLVED_IMPORT_SOURCE) and
                    payload.get("environment_hash") == "resolver-env-b" and
                    payload.get("resolve_imports") is True and
                    payload.get("source_path") == "source_diag.gs"
                )
                patch_ok = quickfix_patch_ok or manual_patch_ok or resolved_import_patch_ok or post_replay_import_patch_ok
                synced_state = {
                    **MOCK_STATE,
                    "dirty": True,
                    "can_undo": True,
                    "module": {
                        **MOCK_STATE["module"],
                        "imports": [{
                            "path": path,
                            "is_native": False,
                            "loaded": True,
                            "normalized_path": path,
                        } for path in loaded_imports],
                        "graphs": [
                            {
                                **MOCK_STATE["module"]["graphs"][0],
                                "parameters": [
                                    {
                                        "name": "speed",
                                        "type": "float",
                                        "direction": "in",
                                        "default": "",
                                        "annotations": [],
                                    }
                                ],
                            }
                        ],
                    },
                    "command_log": [SOURCE_PATCH_COMMAND] if quickfix_patch_ok else [SOURCE_PATCH_COMMAND, MANUAL_PATCH_COMMAND],
                }
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({
                        "ok": patch_ok,
                        "state": synced_state if patch_ok else MOCK_STATE,
                        "error": "" if patch_ok else "unexpected source patch",
                    }),
                )

            page.route("**/api/source_patch", handle_source_patch)

            def handle_source_apply(route):
                body = route.request.post_data or ""
                source_apply_requests.append(body)
                apply_ok = body == MANUAL_SOURCE
                synced_state = {
                    **MOCK_STATE,
                    "dirty": True,
                    "can_undo": True,
                    "module": {
                        **MOCK_STATE["module"],
                        "graphs": [
                            {
                                **MOCK_STATE["module"]["graphs"][0],
                                "parameters": [
                                    {
                                        "name": "speed",
                                        "type": "float",
                                        "direction": "in",
                                        "default": "",
                                        "annotations": [],
                                    }
                                ],
                                "nodes": [{"type": "PrintString", "instance": "writer", "init": "", "annotations": []}],
                            }
                        ],
                    },
                    "command_log": [SOURCE_PATCH_COMMAND, MANUAL_SOURCE_COMMAND],
                }
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({
                        "ok": apply_ok,
                        "state": synced_state if apply_ok else MOCK_STATE,
                        "error": "" if apply_ok else "unexpected manual source",
                    }),
                )

            page.route("**/api/source", handle_source_apply)

            loaded_imports = []
            pending_slow_import_routes = []

            def handle_exec(route):
                payload = json.loads(route.request.post_data or "{}")
                command = payload.get("command", "")
                exec_commands.append(command)
                if command == "import nested.d.gs" and "nested.d.gs" not in loaded_imports:
                    loaded_imports.append("nested.d.gs")
                if command == "import custom.d.gs" and "custom.d.gs" not in loaded_imports:
                    loaded_imports.append("custom.d.gs")
                if command == "import failing-child.d.gs" and "failing-child.d.gs" not in loaded_imports:
                    loaded_imports.append("failing-child.d.gs")
                if command == "import slow.d.gs":
                    pending_slow_import_routes.append(route)
                    return
                response_state = {**MOCK_STATE, "command_log": exec_commands}
                if loaded_imports:
                    response_state = {
                        **response_state,
                        "module": {
                            **MOCK_STATE["module"],
                            "imports": [{
                                "path": path,
                                "is_native": False,
                                "loaded": True,
                                "normalized_path": path,
                            } for path in loaded_imports],
                        },
                    }
                if command == "import failing-parent.d.gs":
                    route.fulfill(
                        status=200,
                        content_type="application/json",
                        body=json.dumps({
                            "ok": False,
                            "command": command,
                            "output": "[ERROR] Cannot read file: failing-parent.d.gs",
                            "error": "[ERROR] Cannot read file: failing-parent.d.gs",
                            "state": response_state,
                        }),
                    )
                    return
                if command == "import disconnected.d.gs":
                    route.abort("failed")
                    return
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({
                        "ok": True,
                        "command": command,
                        "state": response_state,
                    }),
                )

            page.route("**/api/exec", handle_exec)

            page.goto(URL, wait_until="networkidle", timeout=20000)
            page.wait_for_selector(".node-card:has-text('logger')", timeout=10000)
            activate_workbench_tab(page, "SOURCE")
            page.get_by_title("Refresh source diagnostics").click()
            activate_workbench_tab(page, "DIAGNOSTICS")
            page.wait_for_selector("text=GS_TEST_SOURCE_RANGE", timeout=10000)
            page.wait_for_selector("text=Insert ':'", timeout=10000)
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_selector("text=in speed float;", timeout=10000)
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.wait_for_selector('[data-source-env-notice="true"]', timeout=5000)
            env_notice_visible = "current session Environment" in page.locator('[data-source-env-notice="true"]').inner_text()
            page.screenshot(path=OUT / "01_source_diagnostic_loaded.png", full_page=True)

            activate_workbench_tab(page, "DIAGNOSTICS")
            page.get_by_role("button", name="Create graph from diagnostic").click()
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=10000)
            page.wait_for_timeout(500)

            activate_workbench_tab(page, "DIAGNOSTICS")
            page.locator("button:has-text('2:14-2:19')").first.click()
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_function(
                "() => window.__graphScriptSourceEditor && window.__graphScriptSourceEditor.getSelectedText() === 'float'",
                timeout=5000,
            )
            page.screenshot(path=OUT / "02_source_range_focused.png", full_page=True)

            activate_workbench_tab(page, "DIAGNOSTICS")
            page.get_by_role("button", name="Insert ':'").click()
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_selector('[data-source-sync-state="synced_patch"]', timeout=10000)
            page.wait_for_function(
                "() => window.__graphScriptSourceEditor && window.__graphScriptSourceEditor.getValue().includes('in speed : float;')",
                timeout=10000,
            )
            activate_workbench_tab(page, "DIAGNOSTICS")
            page.wait_for_selector("text=No diagnostics", timeout=5000)
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_function(
                "() => window.__graphScriptSourceEditor && window.__graphScriptSourceEditor.getSelectedText() === ': '",
                timeout=5000,
            )
            activate_workbench_tab(page, "CONSOLE")
            page.wait_for_selector("text=apply_source_patch", timeout=5000)
            activate_workbench_tab(page, "SOURCE")
            page.screenshot(path=OUT / "03_source_edit_applied.png", full_page=True)
            patch_status_visible = page.locator('[data-source-sync-state="synced_patch"]').count() == 1
            line_focused = source_editor_selection(page) == ": "
            range_highlight = source_editor_selection(page) == ": "
            source_visible = page.locator("text=in speed : float;").count() > 0
            diagnostic_visible = page.locator("text=GS_TEST_SOURCE_RANGE").count() == 0
            source_action_visible = page.locator("text=Insert ':'").count() == 0
            action_executed = "create_graph FixedByAction" in exec_commands
            edit_posted = FIXED_SOURCE in diagnostic_requests
            source_synced = any(
                json.loads(body or "{}").get("replacement") == ": " and
                json.loads(body or "{}").get("base_hash") == SOURCE_HASH and
                json.loads(body or "{}").get("fallback_source") == FIXED_SOURCE
                for body in source_patch_requests
            )

            ensure_source_editing(page)
            editor = page.locator('[data-source-editor="true"]')
            set_source_editor(page, MANUAL_IMPORT_SOURCE)
            page.wait_for_function(
                "() => document.querySelector('[data-source-env-notice=\"true\"]')?.textContent.includes('source import')",
                timeout=5000,
            )
            import_env_notice_visible = (
                "source import" in page.locator('[data-source-env-notice="true"]').inner_text() and
                "not loaded" in page.locator('[data-source-env-notice="true"]').inner_text()
            )
            source_apply_count_before_blocked_import = len(source_apply_requests)
            set_source_editor(page, BLOCKED_IMPORT_SOURCE)
            page.get_by_title("Apply source").click()
            activate_workbench_tab(page, "DIAGNOSTICS")
            page.wait_for_selector("text=GS_IMPORT_PATH_BLOCKED", timeout=5000)
            blocked_import_diagnostic_visible = page.locator("text=GS_IMPORT_PATH_BLOCKED").count() > 0
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_selector('[data-source-resolver-metadata="true"]', timeout=5000)
            page.wait_for_selector('[data-source-import-status="blocked"]', timeout=5000)
            page.wait_for_selector('[data-source-import-tree="true"]', timeout=5000)
            page.wait_for_selector('[data-source-import-problem="true"]', timeout=5000)
            page.wait_for_selector('[data-source-import-message-status="blocked"]', timeout=5000)
            blocked_import_metadata_visible = (
                "Resolver resolved" in page.locator('[data-source-resolver-metadata="true"]').inner_text() and
                "blocked" in page.locator('[data-source-import-status="blocked"]').inner_text()
            )
            blocked_import_tree_visible = page.locator('[data-source-import-tree="true"]').count() == 1
            blocked_import_message_visible = (
                "Import path escapes" in page.locator('[data-source-import-message-status="blocked"]').inner_text()
            )
            blocked_import_has_no_command = (
                page.locator('[data-source-import-status="blocked"][data-source-import-has-command="false"]').count() == 1 and
                page.locator('[data-source-import-command=""]').count() == 0
            )
            blocked_import_used_resolver = any(
                payload.get("source") == BLOCKED_IMPORT_SOURCE and
                payload.get("resolve_imports") is True
                for payload in resolver_requests
            )
            blocked_import_blocks_apply = len(source_apply_requests) == source_apply_count_before_blocked_import

            source_apply_count_before_disconnected_resolver = len(source_apply_requests)
            source_patch_count_before_disconnected_resolver = len(source_patch_requests)
            set_source_editor(page, DISCONNECTED_RESOLVER_IMPORT_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            page.get_by_title("Apply source").click()
            page.wait_for_selector('[data-source-sync-state="error"]', timeout=5000)
            page.wait_for_selector("text=Source import resolver diagnostics failed", timeout=5000)
            disconnected_resolver_reports_import_aware = page.locator('[data-source-sync-detail="true"]').inner_text().startswith("Source import resolver diagnostics failed")
            disconnected_resolver_retains_buffer = DISCONNECTED_RESOLVER_IMPORT_SOURCE == source_editor_value(page)
            disconnected_resolver_blocks_source_apply = len(source_apply_requests) == source_apply_count_before_disconnected_resolver
            disconnected_resolver_blocks_source_patch = len(source_patch_requests) == source_patch_count_before_disconnected_resolver
            disconnected_resolver_clears_metadata = page.locator('[data-source-resolver-metadata="true"]').count() == 0

            source_apply_count_before_multi_status_import = len(source_apply_requests)
            set_source_editor(page, MULTI_STATUS_IMPORT_SOURCE)
            page.get_by_title("Apply source").click()
            activate_workbench_tab(page, "DIAGNOSTICS")
            page.wait_for_selector("text=GS_IMPORT_CYCLE", timeout=5000)
            page.wait_for_selector("text=GS_IMPORT_DEPTH_EXCEEDED", timeout=5000)
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_selector('[data-source-import-status="cycle"]', timeout=5000)
            page.wait_for_selector('[data-source-import-status="too_deep"]', timeout=5000)
            page.wait_for_selector('[data-source-import-status="dependency_error"]', timeout=5000)
            multi_status_tree_visible = page.locator('[data-source-import-tree="true"]').count() == 1
            multi_status_cycle_visible = (
                page.locator('[data-source-import-status="cycle"][data-source-import-problem="true"][data-source-import-has-command="false"]').count() == 1 and
                "source -> a.d.gs -> b.d.gs -> a.d.gs" in page.locator('[data-source-import-status="cycle"]').inner_text() and
                "Import cycle detected" in page.locator('[data-source-import-message-status="cycle"]').inner_text()
            )
            multi_status_too_deep_visible = (
                page.locator('[data-source-import-status="too_deep"][data-source-import-problem="true"][data-source-import-has-command="false"]').count() == 1 and
                "source -> depth-root.d.gs -> depth-child.d.gs" in page.locator('[data-source-import-status="too_deep"]').inner_text() and
                "Maximum import depth exceeded" in page.locator('[data-source-import-message-status="too_deep"]').inner_text()
            )
            multi_status_dependency_visible = (
                page.locator('[data-source-import-status="dependency_error"][data-source-import-problem="true"][data-source-import-has-command="false"]').count() >= 2 and
                "Dependency failed" in page.locator('[data-source-import-message-status="dependency_error"]').first.inner_text()
            )
            multi_status_used_resolver = any(
                payload.get("source") == MULTI_STATUS_IMPORT_SOURCE and
                payload.get("resolve_imports") is True
                for payload in resolver_requests
            )
            multi_status_blocks_apply = len(source_apply_requests) == source_apply_count_before_multi_status_import

            emitted_source["text"] = FIXED_SOURCE
            set_source_editor(page, RESOLVED_IMPORT_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            source_patch_count_before_resolved_import = len(source_patch_requests)
            page.get_by_title("Apply source").click()
            page.wait_for_selector('[data-source-sync-state="synced_patch"]', timeout=5000)
            resolved_import_patch_carries_env_hash = any(
                json.loads(body or "{}").get("fallback_source") == RESOLVED_IMPORT_SOURCE and
                json.loads(body or "{}").get("environment_hash") == "resolver-env-a" and
                json.loads(body or "{}").get("resolve_imports") is True
                for body in source_patch_requests[source_patch_count_before_resolved_import:]
            )
            page.wait_for_selector('[data-source-import-command="import custom.d.gs"]', timeout=5000)
            page.wait_for_selector('[data-source-import-tree="true"]', timeout=5000)
            import_command_visible = page.locator('[data-source-import-command="import custom.d.gs"]').count() == 1
            import_chain_visible = page.locator('[data-source-import-chain="source -> custom.d.gs"]').count() == 1
            nested_import_chain = '[data-source-import-chain="source -> custom.d.gs -> nested.d.gs"]'
            import_tree_visible = page.locator('[data-source-import-tree="true"]').count() == 1
            import_tree_nested_visible = page.locator(nested_import_chain).count() == 1
            import_plan = page.locator('[data-source-import-plan-run="true"]')
            import_plan_visible = import_plan.count() == 1 and import_plan.get_attribute("data-source-import-plan-count") == "2"
            import_plan_commands = (import_plan.get_attribute("data-source-import-plan-commands") or "").splitlines()
            import_plan_order = import_plan_commands == ["import nested.d.gs", "import custom.d.gs"]
            page.locator('[data-source-import-toggle]').first.click()
            page.wait_for_selector(nested_import_chain, state="detached", timeout=5000)
            import_tree_collapses = page.locator(nested_import_chain).count() == 0
            page.locator('[data-source-import-toggle]').first.click()
            page.wait_for_selector(nested_import_chain, timeout=5000)
            import_tree_expands = page.locator(nested_import_chain).count() == 1
            source_patch_count_before_stale_resolver = len(source_patch_requests)
            page.get_by_title("Apply source").click()
            page.wait_for_selector("text=Source imports resolved to a different environment", timeout=5000)
            page.wait_for_selector('[data-source-sync-state="stale"]', timeout=5000)
            stale_resolver_prompt_visible = page.locator('[data-source-sync-detail="true"]').inner_text().startswith("Source imports resolved")
            stale_resolver_blocks_apply = len(source_patch_requests) == source_patch_count_before_stale_resolver
            diagnostics_count_before_import_command = resolved_import_diagnostics_count["value"]
            import_plan_exec_start = len(exec_commands)
            page.locator('[data-source-import-plan-run="true"]').click()
            activate_workbench_tab(page, "CONSOLE")
            page.wait_for_selector("text=import nested.d.gs", timeout=5000)
            page.wait_for_selector("text=import custom.d.gs", timeout=5000)
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_selector('[data-source-import-loaded="true"]', timeout=5000)
            page.wait_for_selector("text=Import replay loaded 2 imports", timeout=5000)
            import_command_executed = "import custom.d.gs" in exec_commands
            import_plan_executed_in_order = exec_commands[import_plan_exec_start:import_plan_exec_start + 2] == [
                "import nested.d.gs",
                "import custom.d.gs",
            ]
            import_command_refreshed_resolver = resolved_import_diagnostics_count["value"] > diagnostics_count_before_import_command
            import_command_marks_loaded = (
                page.locator('[data-source-import-loaded="true"]').count() >= 2 and
                "session" in page.locator('[data-source-import-loaded="true"]').first.inner_text() and
                page.locator('[data-source-import-command="import custom.d.gs"]').is_disabled() and
                page.locator('[data-source-import-command="import nested.d.gs"]').is_disabled()
            )
            import_plan_filters_loaded = page.locator('[data-source-import-plan-run="true"]').count() == 0
            emitted_source["text"] = RESOLVED_IMPORT_SOURCE
            source_patch_count_before_post_replay_apply = len(source_patch_requests)
            source_apply_count_before_post_replay_apply = len(source_apply_requests)
            set_source_editor(page, RESOLVED_IMPORT_EDITED_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            page.get_by_title("Apply source").click()
            page.wait_for_selector('[data-source-sync-state="synced_patch"]', timeout=5000)
            post_replay_apply_carries_refreshed_guard = any(
                json.loads(body or "{}").get("fallback_source") == RESOLVED_IMPORT_EDITED_SOURCE and
                json.loads(body or "{}").get("base_hash") == source_hash(RESOLVED_IMPORT_SOURCE) and
                json.loads(body or "{}").get("environment_hash") == "resolver-env-b" and
                json.loads(body or "{}").get("resolve_imports") is True and
                json.loads(body or "{}").get("source_path") == "source_diag.gs"
                for body in source_patch_requests[source_patch_count_before_post_replay_apply:]
            )
            post_replay_apply_uses_patch_not_snapshot = len(source_apply_requests) == source_apply_count_before_post_replay_apply
            post_replay_apply_reruns_resolver = any(
                payload.get("source") == RESOLVED_IMPORT_EDITED_SOURCE and
                payload.get("resolve_imports") is True
                for payload in resolver_requests
            )
            post_replay_apply_keeps_plan_filtered = page.locator('[data-source-import-plan-run="true"]').count() == 0

            set_source_editor(page, FAILING_REPLAY_IMPORT_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            page.get_by_title("Apply source").click()
            page.wait_for_selector('[data-source-import-plan-run="true"]', timeout=5000)
            single_failing_import_start = len(exec_commands)
            page.locator('[data-source-import-command="import failing-parent.d.gs"]').click()
            page.wait_for_selector('[data-source-sync-state="error"]', timeout=5000)
            page.wait_for_selector("text=Import command failed at import failing-parent.d.gs", timeout=5000)
            single_import_reports_error = (
                "Cannot read file: failing-parent.d.gs" in page.locator('[data-source-sync-detail="true"]').inner_text()
            )
            single_import_does_not_mark_changed = (
                "Session changed by: import failing-parent.d.gs" not in page.locator('[data-source-sync-detail="true"]').inner_text()
            )
            single_import_execs_once = exec_commands[single_failing_import_start:single_failing_import_start + 1] == [
                "import failing-parent.d.gs",
            ]

            failing_plan = page.locator('[data-source-import-plan-run="true"]')
            failing_plan_visible = (
                failing_plan.count() == 1 and
                failing_plan.get_attribute("data-source-import-plan-count") == "3"
            )
            failing_plan_start = len(exec_commands)
            failing_plan.click()
            page.wait_for_selector('[data-source-sync-state="error"]', timeout=5000)
            page.wait_for_selector("text=Import replay failed at import failing-parent.d.gs", timeout=5000)
            failing_plan_reports_error = (
                "Cannot read file: failing-parent.d.gs" in page.locator('[data-source-sync-detail="true"]').inner_text()
            )
            failing_plan_stops_on_failure = exec_commands[failing_plan_start:failing_plan_start + 3] == [
                "import failing-child.d.gs",
                "import failing-parent.d.gs",
            ]
            failing_plan_does_not_run_later = "import after-failure.d.gs" not in exec_commands[failing_plan_start:]
            failing_plan_filters_partial_loaded = (
                page.locator('[data-source-import-plan-run="true"]').count() == 1 and
                page.locator('[data-source-import-plan-run="true"]').get_attribute("data-source-import-plan-count") == "2" and
                page.locator('[data-source-import-command="import failing-child.d.gs"]').is_disabled()
            )
            page.get_by_title("Revert source").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            failing_import_error_reverts = (
                emitted_source["text"] == source_editor_value(page) and
                page.locator('[data-source-sync-state="session"]').count() == 1
            )

            set_source_editor(page, SLOW_REPLAY_IMPORT_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            page.get_by_title("Apply source").click()
            page.wait_for_selector('[data-source-import-plan-run="true"]', timeout=5000)
            slow_plan = page.locator('[data-source-import-plan-run="true"]')
            slow_plan.evaluate("(el) => el.click()")
            page.wait_for_selector("text=Running 1 import command", timeout=5000)
            slow_import_busy_disables_plan = page.wait_for_function(
                "() => document.querySelector('[data-source-import-plan-run=\"true\"]')?.disabled === true",
                timeout=5000,
            ) is not None
            slow_import_busy_disables_single = page.wait_for_function(
                "() => document.querySelector('[data-source-import-command=\"import slow.d.gs\"]')?.disabled === true",
                timeout=5000,
            ) is not None
            slow_import_busy_disables_apply = page.wait_for_function(
                "() => document.querySelector('[title=\"Apply source\"]')?.disabled === true",
                timeout=5000,
            ) is not None
            loaded_imports.append("slow.d.gs")
            pending_slow_import_routes.pop(0).fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({
                    "ok": True,
                    "command": "import slow.d.gs",
                    "state": {
                        **MOCK_STATE,
                        "command_log": exec_commands,
                        "module": {
                            **MOCK_STATE["module"],
                            "imports": [{
                                "path": path,
                                "is_native": False,
                                "loaded": True,
                                "normalized_path": path,
                            } for path in loaded_imports],
                        },
                    },
                }),
            )
            page.wait_for_selector("text=Import replay loaded 1 import", timeout=5000)

            set_source_editor(page, DISCONNECTED_REPLAY_IMPORT_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            page.get_by_title("Apply source").click()
            page.wait_for_selector('[data-source-import-plan-run="true"]', timeout=5000)
            disconnected_plan_start = len(exec_commands)
            page.locator('[data-source-import-plan-run="true"]').click()
            page.wait_for_selector('[data-source-sync-state="error"]', timeout=5000)
            page.wait_for_selector("text=Import replay failed at import disconnected.d.gs", timeout=5000)
            disconnected_import_reports_connection = (
                "connection failed" in page.locator('[data-source-sync-detail="true"]').inner_text()
            )
            disconnected_import_stops_later = (
                exec_commands[disconnected_plan_start:disconnected_plan_start + 2] == ["import disconnected.d.gs"] and
                "import after-disconnect.d.gs" not in exec_commands[disconnected_plan_start:]
            )

            emitted_source["text"] = SOURCE
            set_source_editor(page, MANUAL_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            manual_edit_visible = "PrintString writer" in source_editor_value(page)

            page.get_by_title("Revert source").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            reverted_visible = "in speed float;" in source_editor_value(page)

            source_apply_count_before_failure = len(source_apply_requests)
            set_source_editor(page, MANUAL_INVALID_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            activate_workbench_tab(page, "DIAGNOSTICS")
            page.wait_for_selector("text=GS_TEST_SOURCE_RANGE", state="detached", timeout=5000)
            activate_workbench_tab(page, "SOURCE")
            page.get_by_title("Apply source").click()
            activate_workbench_tab(page, "DIAGNOSTICS")
            page.wait_for_selector("text=GS_TEST_SOURCE_RANGE", timeout=5000)
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            manual_failure_retains_buffer = MANUAL_INVALID_SOURCE == source_editor_value(page)
            manual_failure_blocks_apply = len(source_apply_requests) == source_apply_count_before_failure
            manual_failure_selects_range = source_editor_selection(page) == "float"

            set_source_editor(page, MANUAL_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            page.wait_for_selector('[data-source-patch-summary="true"]', timeout=5000)
            pending_patch_summary_visible = page.locator('[data-source-patch-summary="true"]').inner_text().startswith("Pending patch 2:14-3:21")
            page.get_by_title("Preview source").click()
            page.wait_for_selector('[data-source-line="2"][data-source-pending-patch="true"]', timeout=5000)
            page.wait_for_selector('[data-source-line="3"][data-source-pending-patch="true"]', timeout=5000)
            pending_patch_range_visible = (
                page.locator('[data-source-line="2"][data-source-pending-patch="true"]').count() == 1 and
                page.locator('[data-source-line="3"][data-source-pending-patch="true"]').count() == 1
            )
            page.get_by_title("Edit source").click()
            source_patch_count_before_manual = len(source_patch_requests)
            source_apply_count_before_manual = len(source_apply_requests)
            page.get_by_title("Apply source").click()
            page.wait_for_selector('[data-source-sync-state="synced_patch"]', timeout=5000)
            activate_workbench_tab(page, "CONSOLE")
            page.wait_for_selector("text=apply_source_patch", timeout=5000)
            activate_workbench_tab(page, "SOURCE")
            manual_source_patch_synced = any(
                json.loads(body or "{}").get("replacement") == MANUAL_PATCH_REPLACEMENT and
                json.loads(body or "{}").get("base_hash") == SOURCE_HASH and
                json.loads(body or "{}").get("fallback_source") == MANUAL_SOURCE
                for body in source_patch_requests[source_patch_count_before_manual:]
            )
            manual_source_patch_blocks_snapshot = len(source_apply_requests) == source_apply_count_before_manual
            manual_patch_status_visible = page.locator('[data-source-sync-state="synced_patch"]').count() == 1
            manual_patch_summary_cleared = page.locator('[data-source-patch-summary="true"]').count() == 0

            page.get_by_title("Revert source").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            set_source_editor(page, MANUAL_SOURCE)
            page.wait_for_selector('[data-source-sync-state="edited"]', timeout=5000)
            source_apply_count_before_stale = len(source_apply_requests)
            emitted_source["text"] = STALE_SOURCE
            activate_workbench_tab(page, "CONSOLE")
            page.locator("input[placeholder='add_node PrintString ps1']").fill("create_graph BackendChanged")
            page.locator("input[placeholder='add_node PrintString ps1']").press("Enter")
            activate_workbench_tab(page, "SOURCE")
            page.wait_for_selector('[data-source-sync-state="stale"]', timeout=5000)
            stale_apply_button_enabled = page.get_by_title("Apply source").is_enabled()
            page.get_by_title("Apply source").click()
            page.wait_for_selector("text=Backend source changed since this buffer was loaded", timeout=5000)
            page.wait_for_selector('[data-source-sync-state="stale"]', timeout=5000)
            stale_first_apply_blocks_source = len(source_apply_requests) == source_apply_count_before_stale
            stale_prompt_visible = page.locator('[data-source-sync-detail="true"]').inner_text().startswith("Backend source changed")
            stale_confirm_apply_enabled = page.get_by_title("Apply source").is_enabled()
            page.get_by_title("Apply source").click()
            page.wait_for_selector('[data-source-sync-state="synced_snapshot"]', timeout=5000)
            activate_workbench_tab(page, "CONSOLE")
            page.wait_for_selector("text=apply_source_b64", timeout=5000)
            activate_workbench_tab(page, "SOURCE")
            page.screenshot(path=OUT / "04_manual_source_applied.png", full_page=True)

            manual_source_synced = MANUAL_SOURCE in source_apply_requests
            undo_enabled = page.get_by_role("button", name=re.compile("Undo")).is_enabled()
            activate_workbench_tab(page, "CONSOLE")
            replay_command_visible = page.locator("text=apply_source_patch").count() > 0
            activate_workbench_tab(page, "SOURCE")
            manual_snapshot_status_visible = page.locator('[data-source-sync-state="synced_snapshot"]').count() == 1

            results = [
                ("source diagnostic cleared", diagnostic_visible),
                ("source edit action cleared", source_action_visible),
                ("source edit applied to preview", source_visible),
                ("source line focused", line_focused),
                ("inserted text highlighted", range_highlight),
                ("edited source rechecked", edit_posted),
                ("edited source synced to session", source_synced),
                ("source sync enables undo", undo_enabled),
                ("source sync logs replay command", replay_command_visible),
                ("source patch status visible", patch_status_visible),
                ("source environment notice visible", env_notice_visible),
                ("source import environment notice visible", import_env_notice_visible),
                ("blocked import diagnostic visible", blocked_import_diagnostic_visible),
                ("blocked import metadata visible", blocked_import_metadata_visible),
                ("blocked import tree visible", blocked_import_tree_visible),
                ("blocked import message visible", blocked_import_message_visible),
                ("blocked import has no command", blocked_import_has_no_command),
                ("blocked import used resolver", blocked_import_used_resolver),
                ("blocked import blocks session apply", blocked_import_blocks_apply),
                ("disconnected resolver reports import aware", disconnected_resolver_reports_import_aware),
                ("disconnected resolver retains buffer", disconnected_resolver_retains_buffer),
                ("disconnected resolver blocks source apply", disconnected_resolver_blocks_source_apply),
                ("disconnected resolver blocks source patch", disconnected_resolver_blocks_source_patch),
                ("disconnected resolver clears metadata", disconnected_resolver_clears_metadata),
                ("multi status import tree visible", multi_status_tree_visible),
                ("multi status import cycle visible", multi_status_cycle_visible),
                ("multi status import too deep visible", multi_status_too_deep_visible),
                ("multi status import dependency visible", multi_status_dependency_visible),
                ("multi status import used resolver", multi_status_used_resolver),
                ("multi status import blocks session apply", multi_status_blocks_apply),
                ("resolved import patch carries environment hash", resolved_import_patch_carries_env_hash),
                ("resolved import command visible", import_command_visible),
                ("resolved import chain visible", import_chain_visible),
                ("resolved import tree visible", import_tree_visible),
                ("resolved import tree nested visible", import_tree_nested_visible),
                ("resolved import plan visible", import_plan_visible),
                ("resolved import plan order", import_plan_order),
                ("resolved import tree collapses", import_tree_collapses),
                ("resolved import tree expands", import_tree_expands),
                ("resolved import command executed", import_command_executed),
                ("resolved import plan executed in order", import_plan_executed_in_order),
                ("resolved import command refreshes resolver", import_command_refreshed_resolver),
                ("resolved import command marks loaded", import_command_marks_loaded),
                ("resolved import plan filters loaded", import_plan_filters_loaded),
                ("post replay apply carries refreshed guard", post_replay_apply_carries_refreshed_guard),
                ("post replay apply uses patch not snapshot", post_replay_apply_uses_patch_not_snapshot),
                ("post replay apply reruns resolver", post_replay_apply_reruns_resolver),
                ("post replay apply keeps plan filtered", post_replay_apply_keeps_plan_filtered),
                ("single import reports error", single_import_reports_error),
                ("single import does not mark changed", single_import_does_not_mark_changed),
                ("single import execs once", single_import_execs_once),
                ("failing import plan visible", failing_plan_visible),
                ("failing import plan reports error", failing_plan_reports_error),
                ("failing import plan stops on failure", failing_plan_stops_on_failure),
                ("failing import plan does not run later", failing_plan_does_not_run_later),
                ("failing import plan filters partial loaded", failing_plan_filters_partial_loaded),
                ("failing import error reverts", failing_import_error_reverts),
                ("slow import busy disables plan", slow_import_busy_disables_plan),
                ("slow import busy disables single", slow_import_busy_disables_single),
                ("slow import busy disables apply", slow_import_busy_disables_apply),
                ("disconnected import reports connection", disconnected_import_reports_connection),
                ("disconnected import stops later", disconnected_import_stops_later),
                ("stale resolver prompt visible", stale_resolver_prompt_visible),
                ("stale resolver blocks apply", stale_resolver_blocks_apply),
                ("manual source edit visible", manual_edit_visible),
                ("manual source revert visible", reverted_visible),
                ("manual source failure retains buffer", manual_failure_retains_buffer),
                ("manual source failure blocks session apply", manual_failure_blocks_apply),
                ("manual source failure selects diagnostic range", manual_failure_selects_range),
                ("manual source pending patch summary visible", pending_patch_summary_visible),
                ("manual source pending patch range visible", pending_patch_range_visible),
                ("manual source patch synced", manual_source_patch_synced),
                ("manual source patch blocks snapshot", manual_source_patch_blocks_snapshot),
                ("manual source patch status visible", manual_patch_status_visible),
                ("manual source patch summary cleared", manual_patch_summary_cleared),
                ("stale source apply button enabled", stale_apply_button_enabled),
                ("stale source first apply blocks session write", stale_first_apply_blocks_source),
                ("stale source prompt visible", stale_prompt_visible),
                ("stale source confirm apply enabled", stale_confirm_apply_enabled),
                ("manual source synced to session", manual_source_synced),
                ("manual source snapshot status visible", manual_snapshot_status_visible),
                ("diagnostic action command executed", action_executed),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nSource range preview regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
