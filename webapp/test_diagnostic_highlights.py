#!/usr/bin/env python3
"""E2E regression for DiagnosticTarget canvas highlights."""
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
URL = "http://127.0.0.1:5176/"
OUT = ROOT / "test_screenshots" / "diagnostic_highlights"
OUT.mkdir(parents=True, exist_ok=True)


def diag(severity, message, code, context, target, source_range=None, actions=None):
    empty_range = {
        "start": {"line": 1, "column": 1},
        "end": {"line": 1, "column": 1},
    }
    full_target = {
        "graph": "",
        "block_kind": "",
        "block_name": "",
        "node_instance": "",
        "pin_name": "",
        "parameter_name": "",
        "reference": "",
        "connection_kind": "",
    }
    full_target.update(target)
    return {
        "severity": severity,
        "message": message,
        "context": context,
        "code": code,
        "range": source_range or empty_range,
        "hint": "Injected by diagnostic highlight regression.",
        "target": full_target,
        "actions": actions or [],
    }


EMITTED_SOURCE = "\n".join([
    "Graph DiagGraph {",
    "    Branch branch{};",
    "    PrintString printer{};",
    "    event BeginPlay {",
    "        flow branch.onTrue -> printer.enter;",
    "    }",
    "}",
])


MOCK_STATE = {
    "file_path": "diagnostic_highlights.gs",
    "dirty": False,
    "active_graph": 0,
    "can_undo": False,
    "can_redo": False,
    "module": {
        "imports": [],
        "lets": [],
        "graphs": [
            {
                "name": "DiagGraph",
                "base_type": None,
                "annotations": [],
                "parameters": [
                    {
                        "name": "speed",
                        "type": "float",
                        "direction": "in",
                        "default": "",
                        "annotations": [],
                    },
                ],
                "nodes": [
                    {
                        "type": "Branch",
                        "instance": "branch",
                        "init": "",
                        "annotations": [{"name": "Position", "args": [{"name": "X", "value": "220"}, {"name": "Y", "value": "130"}]}],
                    },
                    {
                        "type": "PrintString",
                        "instance": "printer",
                        "init": "",
                        "annotations": [{"name": "Position", "args": [{"name": "X", "value": "570"}, {"name": "Y", "value": "120"}]}],
                    },
                    {
                        "type": "GetPlayerCharacter",
                        "instance": "player",
                        "init": "",
                        "annotations": [{"name": "Position", "args": [{"name": "X", "value": "215"}, {"name": "Y", "value": "360"}]}],
                    },
                    {
                        "type": "PrintString",
                        "instance": "idle",
                        "init": "",
                        "annotations": [{"name": "Position", "args": [{"name": "X", "value": "570"}, {"name": "Y", "value": "360"}]}],
                    },
                ],
                "events": [
                    {
                        "name": "BeginPlay",
                        "kind": "event",
                        "annotations": [],
                        "flows": [
                            {"from_node": "branch", "from_pin": "onTrue", "to_node": "printer", "to_pin": "enter", "annotations": []}
                        ],
                        "links": [
                            {"target_node": "printer", "target_pin": "message", "source_node": "player", "source_pin": "character", "annotations": []}
                        ],
                    }
                ],
                "functions": [],
            },
            {
                "name": "OtherGraph",
                "base_type": None,
                "annotations": [],
                "parameters": [],
                "nodes": [
                    {
                        "type": "PrintString",
                        "instance": "other",
                        "init": "",
                        "annotations": [{"name": "Position", "args": [{"name": "X", "value": "260"}, {"name": "Y", "value": "150"}]}],
                    },
                ],
                "events": [],
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
    "diagnostics": [
        diag(
            "warning",
            "Node-level diagnostic",
            "GS_TEST_NODE_TARGET",
            "branch",
            {"graph": "DiagGraph", "node_instance": "branch"},
        ),
        diag(
            "error",
            "Exec connection diagnostic",
            "GS_TEST_EXEC_CONNECTION",
            "branch",
            {
                "graph": "DiagGraph",
                "block_kind": "event",
                "block_name": "BeginPlay",
                "node_instance": "branch",
                "pin_name": "onTrue",
                "connection_kind": "exec",
            },
            {
                "start": {"line": 5, "column": 21},
                "end": {"line": 5, "column": 27},
            },
        ),
        diag(
            "warning",
            "Data connection diagnostic",
            "GS_TEST_DATA_CONNECTION",
            "printer",
            {
                "graph": "DiagGraph",
                "block_kind": "event",
                "block_name": "BeginPlay",
                "node_instance": "printer",
                "pin_name": "message",
                "connection_kind": "data",
            },
        ),
        diag(
            "warning",
            "Parameter diagnostic",
            "GS_TEST_PARAMETER_TARGET",
            "not-param-context",
            {"graph": "DiagGraph", "parameter_name": "speed"},
        ),
        diag(
            "warning",
            "Reference diagnostic",
            "GS_TEST_REFERENCE_TARGET",
            "not-a-node-context",
            {"graph": "DiagGraph", "reference": "player"},
        ),
        diag(
            "error",
            "Other graph diagnostic",
            "GS_TEST_OTHER_GRAPH",
            "other",
            {"graph": "OtherGraph", "node_instance": "other"},
            actions=[
                {
                    "title": "Fix other graph",
                    "kind": "quickfix",
                    "command": "add_node PrintString fixed_other",
                }
            ],
        ),
    ],
    "command_log": [],
}

MOCK_STATE["module_diagnostics"] = json.loads(json.dumps(MOCK_STATE["diagnostics"]))
MOCK_STATE["diagnostics"] = [
    diagnostic
    for diagnostic in MOCK_STATE["diagnostics"]
    if diagnostic["target"]["graph"] == "DiagGraph"
]


def state_with_active_graph(index):
    state = json.loads(json.dumps(MOCK_STATE))
    state["active_graph"] = index
    return state


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
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5176"],
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


def border_color(locator):
    return locator.evaluate("el => getComputedStyle(el).borderColor")


def font_weight(locator):
    return int(locator.evaluate("el => getComputedStyle(el).fontWeight"))


def main():
    proc = start_vite()
    results = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1500, "height": 950})

            page.route("**/api/state", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps(MOCK_STATE),
            ))
            page.route("**/api/diagnostics", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({"ok": True, "stage": "session", "diagnostics": []}),
            ))
            page.route("**/api/emit", lambda route: route.fulfill(status=200, content_type="text/plain", body=EMITTED_SOURCE))
            exec_commands = []
            active_graph = {"value": 0}

            def handle_exec(route):
                payload = json.loads(route.request.post_data or "{}")
                command = payload.get("command", "")
                exec_commands.append(command)
                if command == "switch_graph 1":
                    active_graph["value"] = 1
                elif command == "switch_graph 0":
                    active_graph["value"] = 0
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "state": state_with_active_graph(active_graph["value"])}),
                )

            page.route("**/api/exec", handle_exec)

            page.goto(URL, wait_until="networkidle", timeout=20000)
            page.wait_for_selector(".node-card:has-text('branch')", timeout=10000)
            page.wait_for_selector("text=GS_TEST_EXEC_CONNECTION", timeout=10000)
            page.wait_for_timeout(700)

            page.screenshot(path=OUT / "01_loaded.png", full_page=True)

            branch = page.locator(".node-card:has-text('branch')").first
            printer = page.locator(".node-card:has-text('printer')").first
            player = page.locator(".node-card:has-text('player')").first
            idle = page.locator(".node-card:has-text('idle')").first
            branch_on_true = branch.locator("text=onTrue").first
            printer_message = printer.locator("text=message").first

            branch_highlighted = border_color(branch) != "rgb(0, 0, 0)" and font_weight(branch_on_true) >= 600
            printer_pin_highlighted = font_weight(printer_message) >= 600
            reference_node_highlighted = border_color(player) != border_color(idle)
            edge_highlighted = page.locator(".gs-diagnostic-line-error, .gs-diagnostic-line-warning").count() >= 1
            diagnostics_visible = all(
                page.locator(f"text={code}").count() > 0
                for code in [
                    "GS_TEST_NODE_TARGET",
                    "GS_TEST_EXEC_CONNECTION",
                    "GS_TEST_DATA_CONNECTION",
                    "GS_TEST_PARAMETER_TARGET",
                    "GS_TEST_REFERENCE_TARGET",
                ]
            )
            module_diagnostic_visible = page.locator("text=GS_TEST_OTHER_GRAPH").count() > 0
            parameter_highlighted = (
                page.locator('[data-graph-param="speed"][data-param-diagnostic-severity="warning"]').count() == 1
            )

            page.get_by_role("button", name="speed", exact=True).click()
            page.wait_for_selector('[data-graph-param="speed"][data-param-diagnostic-focused="true"]', timeout=5000)
            parameter_focused = (
                page.locator('[data-graph-param="speed"][data-param-diagnostic-focused="true"]').count() == 1
            )

            page.locator('[data-diagnostic-locate="GS_TEST_REFERENCE_TARGET"]').click()
            page.wait_for_selector('[data-node-instance="player"]', timeout=5000)
            reference_target_selected = (
                page.locator('[data-node-instance="player"]').count() == 1
            )

            locate = page.locator("button:has-text('branch.onTrue')").first
            exec_diagnostic_label = locate.inner_text()
            exec_diagnostic_label_structured = (
                "event BeginPlay" in exec_diagnostic_label
                and "branch.onTrue" in exec_diagnostic_label
                and "exec" in exec_diagnostic_label
            )
            locate.click()
            page.wait_for_timeout(500)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            page.wait_for_selector('[data-source-range="active"]', timeout=5000)
            page.screenshot(path=OUT / "02_focused_exec_diagnostic.png", full_page=True)
            focused_line = page.locator(".gs-diagnostic-line-focused").count() >= 1
            focused_source_line = page.locator('[data-source-line="5"][data-source-focused="true"]').count() == 1
            focused_source_range = page.locator('[data-source-range="active"]').inner_text() == "onTrue"

            page.get_by_role("button", name="branch", exact=True).click()
            page.wait_for_timeout(300)
            source_range_cleared = page.locator('[data-source-range="active"]').count() == 0

            search_box = page.get_by_label("Search graph")
            search_box.fill("player")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_REFERENCE_TARGET"]',
                timeout=5000,
            )
            search_reference_diagnostic_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_REFERENCE_TARGET"]'
                ).count() == 1
            )
            search_reference_diagnostic_detail = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_REFERENCE_TARGET"] [data-graph-search-result-detail]'
                ).first.inner_text().find("player") != -1
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_REFERENCE_TARGET"]'
            ).click()
            page.wait_for_selector('[data-node-instance="player"]', timeout=5000)
            search_reference_diagnostic_selects_node = (
                page.locator('[data-node-instance="player"]').count() == 1
            )

            search_box.fill("speed")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_PARAMETER_TARGET"]',
                timeout=5000,
            )
            search_parameter_diagnostic_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_PARAMETER_TARGET"]'
                ).count() == 1
            )
            search_parameter_diagnostic_detail = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_PARAMETER_TARGET"] [data-graph-search-result-detail]'
                ).first.inner_text().find("speed") != -1
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_PARAMETER_TARGET"]'
            ).click()
            page.wait_for_selector('[data-graph-param="speed"][data-param-diagnostic-focused="true"]', timeout=5000)
            search_parameter_diagnostic_focuses_param = (
                page.locator('[data-graph-param="speed"][data-param-diagnostic-focused="true"]').count() == 1
            )

            search_box.fill("onTrue")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_EXEC_CONNECTION"]',
                timeout=5000,
            )
            search_pin_diagnostic_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_EXEC_CONNECTION"]'
                ).count() == 1
            )
            search_pin_diagnostic_detail_text = page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_EXEC_CONNECTION"] [data-graph-search-result-detail]'
            ).first.inner_text()
            search_pin_diagnostic_detail = (
                "onTrue" in search_pin_diagnostic_detail_text
                and "BeginPlay" in search_pin_diagnostic_detail_text
                and "exec" in search_pin_diagnostic_detail_text
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_EXEC_CONNECTION"]'
            ).click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            page.wait_for_selector('[data-source-range="active"]', timeout=5000)
            search_pin_diagnostic_focuses_source = (
                page.locator('[data-source-range="active"]').inner_text() == "onTrue"
            )

            search_box.fill("5:21")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_EXEC_CONNECTION"]',
                timeout=5000,
            )
            search_range_diagnostic_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_EXEC_CONNECTION"]'
                ).count() == 1
            )
            search_range_diagnostic_detail = (
                "5:21-5:27" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_EXEC_CONNECTION"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="diagnostic"][data-graph-search-result-label="GS_TEST_EXEC_CONNECTION"]'
            ).click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            page.wait_for_selector('[data-source-range="active"]', timeout=5000)
            search_range_diagnostic_focuses_source = (
                page.locator('[data-source-range="active"]').inner_text() == "onTrue"
            )

            search_box.fill("ohter")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="other"]',
                timeout=5000,
            )
            search_typo_node_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="other"][data-graph-search-result-match="typo"]'
                ).count() == 1
            )
            search_typo_node_first = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-label]'
                ).first.get_attribute("data-graph-search-result-label") == "other"
            )
            search_typo_node_highlighted = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="other"] [data-graph-search-result-title] [data-graph-search-typo-match]'
                ).evaluate_all("items => items.map(item => item.textContent).join('')") == "th"
            )

            search_box.fill("otg")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="graph"][data-graph-search-result-label="OtherGraph"]',
                timeout=5000,
            )
            search_fuzzy_graph_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="graph"][data-graph-search-result-label="OtherGraph"][data-graph-search-result-match="fuzzy"]'
                ).count() == 1
            )
            search_fuzzy_graph_highlighted = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="graph"][data-graph-search-result-label="OtherGraph"] [data-graph-search-result-title] [data-graph-search-fuzzy-match]'
                ).evaluate_all("items => items.map(item => item.textContent).join('')") == "OtG"
            )
            page.mouse.move(20, 20)
            search_box.fill("other")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="other"]',
                timeout=5000,
            )
            search_cross_graph_result_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="other"]'
                ).count() == 1
            )
            search_scored_exact_node_first = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-label]'
                ).first.get_attribute("data-graph-search-result-label") == "other"
            )
            search_scored_order = page.locator(
                '[data-graph-search-results] [data-graph-search-result-label]'
            ).evaluate_all(
                "items => items.slice(0, 2).map(item => item.getAttribute('data-graph-search-result-label')).join('|')"
            ) == "other|OtherGraph"
            search_group_header_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-group-header="OtherGraph"]'
                ).count() == 1
            )
            search_group_contains_node = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-group="OtherGraph"] [data-graph-search-result-kind="node"][data-graph-search-result-label="other"]'
                ).count() == 1
            )
            search_label_match_highlighted = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="graph"][data-graph-search-result-label="OtherGraph"] [data-graph-search-result-title] [data-graph-search-match]'
                ).first.inner_text() == "Other"
            )
            search_detail_match_highlighted = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="other"] [data-graph-search-result-detail] [data-graph-search-match]'
                ).first.inner_text() == "Other"
            )
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="other"][data-graph-search-result-active="true"]',
                timeout=5000,
            )
            search_box.press("ArrowDown")
            search_keyboard_active_graph = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="graph"][data-graph-search-result-label="OtherGraph"][data-graph-search-result-active="true"]'
                ).count() == 1
            )
            search_box.press("ArrowUp")
            search_keyboard_returns_to_node = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="other"][data-graph-search-result-active="true"]'
                ).count() == 1
            )
            search_box.press("Enter")
            page.wait_for_selector(".node-card:has-text('other')", timeout=5000)
            page.wait_for_timeout(300)
            search_cross_graph_switched = page.locator("select").first.input_value() == "1"
            search_cross_graph_node_visible = page.locator(".node-card:has-text('other')").count() >= 1
            search_cross_graph_switch_command = "switch_graph 1" in exec_commands

            page.select_option("select", "0")
            page.wait_for_selector(".node-card:has-text('branch')", timeout=5000)

            page.locator("button:has-text('other')").first.click()
            page.wait_for_selector(".node-card:has-text('other')", timeout=5000)
            page.wait_for_timeout(500)
            page.screenshot(path=OUT / "03_other_graph_diagnostic.png", full_page=True)
            switched_graph = page.locator("select").first.input_value() == "1"
            other_node_visible = page.locator(".node-card:has-text('other')").count() >= 1
            switch_command_executed = "switch_graph 1" in exec_commands

            page.select_option("select", "0")
            page.wait_for_selector(".node-card:has-text('branch')", timeout=5000)
            action_start = len(exec_commands)
            page.get_by_role("button", name="Fix other graph").click()
            page.wait_for_selector(".node-card:has-text('other')", timeout=5000)
            page.wait_for_timeout(500)
            action_commands = exec_commands[action_start:action_start + 2]
            quickfix_switches_before_command = action_commands == [
                "switch_graph 1",
                "add_node PrintString fixed_other",
            ]

            results = [
                ("diagnostics visible", diagnostics_visible),
                ("module diagnostics visible", module_diagnostic_visible),
                ("node target highlighted", branch_highlighted),
                ("pin target highlighted", printer_pin_highlighted),
                ("reference target highlighted", reference_node_highlighted),
                ("connection target highlighted", edge_highlighted),
                ("parameter target highlighted", parameter_highlighted),
                ("parameter target focused", parameter_focused),
                ("reference target selected", reference_target_selected),
                ("connection diagnostic label includes block", exec_diagnostic_label_structured),
                ("focused diagnostic highlighted", focused_line),
                ("source range focused from diagnostic", focused_source_line),
                ("source token highlighted from diagnostic", focused_source_range),
                ("source range cleared by unranged diagnostic", source_range_cleared),
                ("search finds reference diagnostic", search_reference_diagnostic_visible),
                ("search reference diagnostic detail includes node", search_reference_diagnostic_detail),
                ("search reference diagnostic selects node", search_reference_diagnostic_selects_node),
                ("search finds parameter diagnostic", search_parameter_diagnostic_visible),
                ("search parameter diagnostic detail includes param", search_parameter_diagnostic_detail),
                ("search parameter diagnostic focuses param", search_parameter_diagnostic_focuses_param),
                ("search finds pin diagnostic", search_pin_diagnostic_visible),
                ("search pin diagnostic detail includes target", search_pin_diagnostic_detail),
                ("search pin diagnostic focuses source", search_pin_diagnostic_focuses_source),
                ("search finds range diagnostic", search_range_diagnostic_visible),
                ("search range diagnostic detail includes span", search_range_diagnostic_detail),
                ("search range diagnostic focuses source", search_range_diagnostic_focuses_source),
                ("cross graph typo search finds node", search_typo_node_visible),
                ("cross graph typo search ranks node first", search_typo_node_first),
                ("cross graph typo search highlights difference", search_typo_node_highlighted),
                ("cross graph fuzzy search finds graph", search_fuzzy_graph_visible),
                ("cross graph fuzzy search highlights characters", search_fuzzy_graph_highlighted),
                ("cross graph search result visible", search_cross_graph_result_visible),
                ("cross graph search ranks exact node first", search_scored_exact_node_first),
                ("cross graph search sorted by score", search_scored_order),
                ("cross graph search group header visible", search_group_header_visible),
                ("cross graph search group contains node", search_group_contains_node),
                ("cross graph search highlights label match", search_label_match_highlighted),
                ("cross graph search highlights detail match", search_detail_match_highlighted),
                ("cross graph search arrow key selects graph", search_keyboard_active_graph),
                ("cross graph search arrow key returns to node", search_keyboard_returns_to_node),
                ("cross graph search switches graph", search_cross_graph_switched),
                ("cross graph search shows target node", search_cross_graph_node_visible),
                ("cross graph search records switch command", search_cross_graph_switch_command),
                ("cross graph diagnostic switches graph", switched_graph),
                ("cross graph diagnostic shows target node", other_node_visible),
                ("cross graph locate records switch command", switch_command_executed),
                ("cross graph quickfix switches before command", quickfix_switches_before_command),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nDiagnostic highlight regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
