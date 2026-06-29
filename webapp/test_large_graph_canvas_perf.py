#!/usr/bin/env python3
"""1000+ node Web canvas performance smoke for FlowGram rendering."""
import argparse
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

NODE_COUNT = 1000
EVENT_COUNT = 20
RENDER_BUDGET_MS = 30000
INTERACTION_BUDGET_MS = 5000
DIAGNOSTIC_LOCATE_BUDGET_MS = INTERACTION_BUDGET_MS


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mode",
        choices=["dev", "preview"],
        default="dev",
        help="Start either Vite dev server or production vite preview.",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=None,
        help="Override server port. Defaults to 5180 for dev and 5181 for preview.",
    )
    return parser.parse_args()


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


def large_state():
    nodes = []
    columns = 50
    for i in range(NODE_COUNT):
        type_name = "PrintString" if i % 2 == 0 else "Delay"
        nodes.append(positioned_node(
            type_name,
            f"n{i}",
            120 + (i % columns) * 260,
            120 + (i // columns) * 150,
        ))

    events = []
    nodes_per_event = NODE_COUNT // EVENT_COUNT
    for event_index in range(EVENT_COUNT):
        start = event_index * nodes_per_event
        end = start + nodes_per_event
        flows = []
        for i in range(start, end - 1):
            flows.append({
                "from_node": f"n{i}",
                "from_pin": "exit" if i % 2 == 0 else "completed",
                "to_node": f"n{i + 1}",
                "to_pin": "enter",
            })
        events.append({
            "name": f"E{event_index}",
            "kind": "event",
            "flows": flows,
            "links": [],
        })

    return {
        "file_path": "large_canvas_perf.gs",
        "dirty": False,
        "active_graph": 0,
        "can_undo": False,
        "can_redo": False,
        "module": {
            "imports": [],
            "lets": [],
            "graphs": [
                {
                    "name": "LargeCanvas",
                    "base_type": None,
                    "annotations": [],
                    "parameters": [],
                    "nodes": nodes,
                    "events": events,
                    "functions": [],
                },
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
                    {"name": "message", "kind": "data", "direction": "in", "type": "FString"},
                ],
            },
            {
                "type_name": "Delay",
                "is_native": True,
                "source_graph": "",
                "tags": [],
                "pins": [
                    {"name": "enter", "kind": "exec", "direction": "in", "type": ""},
                    {"name": "completed", "kind": "exec", "direction": "out", "type": ""},
                    {"name": "duration", "kind": "data", "direction": "in", "type": "float"},
                ],
            },
        ],
        "schemas": [],
        "diagnostics": [
            {
                "severity": "warning",
                "message": "Large graph diagnostic focus target",
                "context": "n500",
                "code": "GS_PERF_SMOKE",
                "range": {
                    "start": {"line": 1, "column": 1},
                    "end": {"line": 1, "column": 1},
                },
                "hint": "Injected by large canvas performance smoke.",
                "target": {
                    "graph": "LargeCanvas",
                    "block_kind": "event",
                    "block_name": "E10",
                    "node_instance": "n500",
                    "pin_name": "",
                    "parameter_name": "",
                    "reference": "",
                    "connection_kind": "",
                },
                "actions": [],
            }
        ],
        "command_log": [],
    }


def source_from_state(state):
    graph = state["module"]["graphs"][0]
    return "\n".join([
        f"Graph {graph['name']} {{",
        f"    // {len(graph['nodes'])} nodes omitted by performance smoke",
        "}",
    ])


def update_position_for_command(state, command):
    parts = shlex.split(command)
    if len(parts) < 6 or parts[0:2] != ["annotate", "node"] or parts[3] != "Position":
        return False

    values = {}
    for part in parts[4:]:
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        values[key] = value
    if "X" not in values or "Y" not in values:
        return False

    graph = state["module"]["graphs"][state["active_graph"]]
    for node in graph["nodes"]:
        if node["instance"] != parts[2]:
            continue
        node["annotations"] = [
            annotation for annotation in node["annotations"]
            if annotation["name"] != "Position"
        ]
        node["annotations"].append({
            "name": "Position",
            "args": [
                {"name": "X", "value": values["X"]},
                {"name": "Y", "value": values["Y"]},
            ],
        })
        return True
    return False


def position_of(state, instance):
    graph = state["module"]["graphs"][state["active_graph"]]
    node = next(node for node in graph["nodes"] if node["instance"] == instance)
    annotation = next(annotation for annotation in node["annotations"] if annotation["name"] == "Position")
    values = {arg["name"]: arg["value"] for arg in annotation["args"]}
    return values["X"], values["Y"]


def numeric_position_of(state, instance):
    x, y = position_of(state, instance)
    return int(x), int(y)


def wait_for_breadcrumb(page, part, value, timeout=INTERACTION_BUDGET_MS):
    selector = f"[data-breadcrumb-{part}]"
    deadline = time.time() + (timeout / 1000)
    while time.time() < deadline:
        matched = page.locator(selector).evaluate_all(
            """(elements, expected) => elements.some(el =>
                el.getAttribute(expected.attr) === expected.value &&
                el.textContent.trim() === expected.value
            )""",
            {"attr": f"data-breadcrumb-{part}", "value": value},
        )
        if matched:
            return
        page.wait_for_timeout(100)
    raise AssertionError(f"Breadcrumb {part}={value} not found")



def wait_for_server(url, proc=None):
    deadline = time.time() + 30
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            output = ""
            try:
                output = proc.stdout.read() if proc.stdout else ""
            except Exception:
                output = ""
            raise RuntimeError(f"Vite server exited before becoming ready at {url}\n{output}")
        try:
            with urllib.request.urlopen(url, timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.5)
    raise RuntimeError(f"Vite server did not become ready at {url}")


def start_vite(mode, port, url):
    script = "preview" if mode == "preview" else "dev"
    proc = subprocess.Popen(
        ["npm.cmd", "run", script, "--", "--host", "127.0.0.1", "--port", str(port), "--strictPort"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        wait_for_server(url, proc)
    except Exception:
        stop_process_tree(proc)
        raise
    return proc


def stop_process_tree(proc):
    if proc.poll() is not None:
        return
    if sys.platform.startswith("win"):
        subprocess.run(
            ["taskkill", "/PID", str(proc.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def main():
    args = parse_args()
    port = args.port if args.port is not None else (5181 if args.mode == "preview" else 5180)
    url = f"http://127.0.0.1:{port}/"
    out = ROOT / "test_screenshots" / f"large_graph_canvas_perf_{args.mode}"
    out.mkdir(parents=True, exist_ok=True)
    state = large_state()
    exec_commands = []
    proc = start_vite(args.mode, port, url)
    results = []
    metrics = {}
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1600, "height": 1000})
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
                update_position_for_command(state, command)
                state["dirty"] = True
                state["can_undo"] = True
                state["command_log"] = exec_commands[:]
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "command": command, "state": state}),
                )

            page.route("**/api/exec", handle_exec)

            start = time.perf_counter()
            page.goto(url, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_function(
                f"() => document.querySelectorAll('.node-card').length >= {NODE_COUNT}",
                timeout=RENDER_BUDGET_MS,
            )
            render_ms = round((time.perf_counter() - start) * 1000)
            wait_for_breadcrumb(page, "graph", "LargeCanvas")
            wait_for_breadcrumb(page, "block", "event E0")
            metrics["initial_render_ms"] = render_ms
            metrics["node_cards"] = page.locator(".node-card").count()
            metrics["line_count"] = page.locator('[data-testid="sdk.workflow.canvas.line"]').count()
            page.wait_for_selector("[data-graph-minimap]", timeout=INTERACTION_BUDGET_MS)
            metrics["minimap_nodes"] = page.locator("[data-minimap-node]").count()
            page.screenshot(path=out / "01_large_graph_rendered.png", full_page=True)

            click_start = time.perf_counter()
            page.locator(".node-card:has-text('n0')").first.click()
            page.wait_for_selector('[data-node-instance="n0"]', timeout=INTERACTION_BUDGET_MS)
            wait_for_breadcrumb(page, "node", "n0")
            direct_select_ms = round((time.perf_counter() - click_start) * 1000)
            metrics["direct_select_ms"] = direct_select_ms

            page.wait_for_selector("[data-minimap-viewport]", timeout=INTERACTION_BUDGET_MS)
            minimap_viewport_before = page.locator("[data-minimap-viewport]").get_attribute("data-viewport-center")
            minimap_command_count_before = len(exec_commands)
            minimap_start = time.perf_counter()
            page.locator('[data-minimap-node="n900"]').click()
            page.wait_for_selector('[data-node-instance="n900"]', timeout=INTERACTION_BUDGET_MS)
            wait_for_breadcrumb(page, "node", "n900")
            page.wait_for_selector('[data-minimap-node="n900"][data-minimap-selected="true"]', timeout=INTERACTION_BUDGET_MS)
            page.wait_for_function(
                """initial => {
                    const viewport = document.querySelector("[data-minimap-viewport]");
                    return viewport && viewport.getAttribute("data-viewport-center") !== initial;
                }""",
                arg=minimap_viewport_before,
                timeout=INTERACTION_BUDGET_MS,
            )
            minimap_select_ms = round((time.perf_counter() - minimap_start) * 1000)
            metrics["minimap_select_ms"] = minimap_select_ms
            minimap_viewport_after = page.locator("[data-minimap-viewport]").get_attribute("data-viewport-center")
            metrics["minimap_viewport_shifted"] = minimap_viewport_after != minimap_viewport_before
            metrics["minimap_select_commands"] = len(exec_commands) - minimap_command_count_before

            minimap_drag_command_count_before = len(exec_commands)
            minimap_drag_start = page.locator("[data-graph-minimap] svg").bounding_box()
            page.mouse.move(minimap_drag_start["x"] + 6, minimap_drag_start["y"] + 6)
            page.mouse.down()
            page.mouse.move(
                minimap_drag_start["x"] + minimap_drag_start["width"] * 0.28,
                minimap_drag_start["y"] + minimap_drag_start["height"] * 0.28,
                steps=8,
            )
            page.mouse.up()
            page.wait_for_function(
                """initial => {
                    const viewport = document.querySelector("[data-minimap-viewport]");
                    return viewport && viewport.getAttribute("data-viewport-center") !== initial;
                }""",
                arg=minimap_viewport_after,
                timeout=INTERACTION_BUDGET_MS,
            )
            minimap_drag_viewport_after = page.locator("[data-minimap-viewport]").get_attribute("data-viewport-center")
            metrics["minimap_drag_viewport_shifted"] = minimap_drag_viewport_after != minimap_viewport_after
            metrics["minimap_drag_commands"] = len(exec_commands) - minimap_drag_command_count_before

            restore_viewport_before = minimap_drag_viewport_after
            page.locator('[data-minimap-node="n0"]').click()
            page.wait_for_selector('[data-minimap-node="n0"][data-minimap-selected="true"]', timeout=INTERACTION_BUDGET_MS)
            page.wait_for_function(
                """initial => {
                    const viewport = document.querySelector("[data-minimap-viewport]");
                    return viewport && viewport.getAttribute("data-viewport-center") !== initial;
                }""",
                arg=restore_viewport_before,
                timeout=INTERACTION_BUDGET_MS,
            )

            locate_start = time.perf_counter()
            page.locator("button:has-text('n500')").first.click()
            page.wait_for_selector('[data-node-instance="n500"]', timeout=DIAGNOSTIC_LOCATE_BUDGET_MS)
            wait_for_breadcrumb(page, "block", "event E10", DIAGNOSTIC_LOCATE_BUDGET_MS)
            wait_for_breadcrumb(page, "node", "n500", DIAGNOSTIC_LOCATE_BUDGET_MS)
            locate_ms = round((time.perf_counter() - locate_start) * 1000)
            metrics["diagnostic_locate_ms"] = locate_ms

            search_start = time.perf_counter()
            page.get_by_label("Search graph").fill("n750")
            page.locator('[data-graph-search-results] [data-graph-search-result-label="n750"]').first.click()
            page.wait_for_selector('[data-node-instance="n750"]', timeout=INTERACTION_BUDGET_MS)
            wait_for_breadcrumb(page, "node", "n750")
            search_locate_ms = round((time.perf_counter() - search_start) * 1000)
            metrics["search_locate_ms"] = search_locate_ms

            search_enter_start = time.perf_counter()
            page.get_by_label("Search graph").fill("n751")
            page.get_by_label("Search graph").press("Enter")
            page.wait_for_selector('[data-node-instance="n751"]', timeout=INTERACTION_BUDGET_MS)
            wait_for_breadcrumb(page, "node", "n751")
            search_enter_locate_ms = round((time.perf_counter() - search_enter_start) * 1000)
            metrics["search_enter_locate_ms"] = search_enter_locate_ms

            pan_start = time.perf_counter()
            page.mouse.move(800, 420)
            page.mouse.down()
            page.mouse.move(620, 360, steps=12)
            page.mouse.up()
            page.wait_for_timeout(250)
            pan_ms = round((time.perf_counter() - pan_start) * 1000)
            metrics["pan_ms"] = pan_ms
            metrics["visible_after_pan"] = page.locator(".node-card").count()

            zoom_before = page.locator(".node-card:has-text('n0')").first.bounding_box()
            zoom_start = time.perf_counter()
            page.mouse.move(800, 420)
            page.keyboard.down("Control")
            page.mouse.wheel(0, -900)
            page.keyboard.up("Control")
            page.wait_for_timeout(350)
            zoom_ms = round((time.perf_counter() - zoom_start) * 1000)
            zoom_after = page.locator(".node-card:has-text('n0')").first.bounding_box()
            metrics["zoom_ms"] = zoom_ms
            metrics["zoom_width_before"] = round(zoom_before["width"])
            metrics["zoom_width_after"] = round(zoom_after["width"])

            page.locator('[data-minimap-node="n0"]').click()
            page.wait_for_selector('[data-minimap-node="n0"][data-minimap-selected="true"]', timeout=INTERACTION_BUDGET_MS)
            page.wait_for_timeout(250)

            drag_start_position = numeric_position_of(state, "n0")
            drag_start = time.perf_counter()
            drag_command_count_before = sum(1 for command in exec_commands if command.startswith("annotate node n0 Position "))
            node_box = page.locator(".node-card:has-text('n0')").first.bounding_box()
            drag_screen_dx = 90
            drag_screen_dy = 55
            page.mouse.move(node_box["x"] + node_box["width"] / 2, node_box["y"] + node_box["height"] / 2)
            page.mouse.down()
            page.mouse.move(node_box["x"] + node_box["width"] / 2 + drag_screen_dx, node_box["y"] + node_box["height"] / 2 + drag_screen_dy, steps=16)
            page.mouse.up()
            deadline = time.time() + (INTERACTION_BUDGET_MS / 1000)
            while time.time() < deadline and sum(1 for command in exec_commands if command.startswith("annotate node n0 Position ")) <= drag_command_count_before:
                page.wait_for_timeout(100)
            drag_position_ms = round((time.perf_counter() - drag_start) * 1000)
            drag_command_count_after = sum(1 for command in exec_commands if command.startswith("annotate node n0 Position "))
            drag_end_position = numeric_position_of(state, "n0")
            zoom_ratio = zoom_after["width"] / zoom_before["width"] if zoom_before["width"] else 1
            expected_drag_dx = round(drag_screen_dx / zoom_ratio)
            expected_drag_dy = round(drag_screen_dy / zoom_ratio)
            actual_drag_dx = drag_end_position[0] - drag_start_position[0]
            actual_drag_dy = drag_end_position[1] - drag_start_position[1]
            metrics["zoom_ratio_for_drag"] = round(zoom_ratio, 3)
            metrics["drag_position_dx"] = actual_drag_dx
            metrics["drag_position_dy"] = actual_drag_dy
            metrics["expected_drag_position_dx"] = expected_drag_dx
            metrics["expected_drag_position_dy"] = expected_drag_dy
            metrics["drag_position_commands_added"] = drag_command_count_after - drag_command_count_before
            metrics["drag_position_ms"] = drag_position_ms
            page.screenshot(path=out / "02_after_select_and_pan.png", full_page=True)

            results = [
                ("renders 1000 node cards", metrics["node_cards"] >= NODE_COUNT),
                ("renders expected flow lines", metrics["line_count"] >= NODE_COUNT - EVENT_COUNT),
                ("minimap renders graph overview", metrics["minimap_nodes"] >= NODE_COUNT),
                ("initial render within smoke budget", render_ms < RENDER_BUDGET_MS),
                ("breadcrumb shows graph and active block", page.locator('[data-graph-breadcrumb] [data-breadcrumb-graph="LargeCanvas"]').count() == 1 and page.locator('[data-graph-breadcrumb] [data-breadcrumb-block="event E10"]').count() == 1),
                ("minimap select within smoke budget", minimap_select_ms < INTERACTION_BUDGET_MS),
                ("minimap viewport follows selection", metrics["minimap_viewport_shifted"]),
                ("minimap viewport pan is local", metrics["minimap_select_commands"] == 0),
                ("minimap drag shifts viewport", metrics["minimap_drag_viewport_shifted"]),
                ("minimap drag pan is local", metrics["minimap_drag_commands"] == 0),
                ("direct node select within smoke budget", direct_select_ms < INTERACTION_BUDGET_MS),
                ("drag position replayed", metrics["drag_position_commands_added"] > 0),
                ("drag position changed state", drag_end_position != drag_start_position),
                ("drag position compensates zoom", abs(actual_drag_dx - expected_drag_dx) <= 2 and abs(actual_drag_dy - expected_drag_dy) <= 2),
                ("drag position within smoke budget", drag_position_ms < INTERACTION_BUDGET_MS),
                ("diagnostic locate within smoke budget", locate_ms < DIAGNOSTIC_LOCATE_BUDGET_MS),
                ("graph search locate within smoke budget", search_locate_ms < INTERACTION_BUDGET_MS),
                ("graph search enter locate within smoke budget", search_enter_locate_ms < INTERACTION_BUDGET_MS),
                ("pan within smoke budget", pan_ms < INTERACTION_BUDGET_MS),
                ("zoom changes node scale", abs(metrics["zoom_width_after"] - metrics["zoom_width_before"]) >= 1),
                ("zoom within smoke budget", zoom_ms < INTERACTION_BUDGET_MS),
                ("canvas remains populated after pan", metrics["visible_after_pan"] >= NODE_COUNT),
            ]
            browser.close()
    finally:
        stop_process_tree(proc)

    failed = [name for name, ok in results if not ok]
    print(f"\nLarge graph canvas performance smoke ({args.mode})")
    for key, value in metrics.items():
        print(f"  METRIC {key}={value}")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {out}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
