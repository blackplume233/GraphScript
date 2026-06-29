#!/usr/bin/env python3
"""1000+ node Web canvas smoke against a real gs serve backend."""
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

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
GS_EXE = REPO / "build" / "Release" / "gs.exe"
CORE_DECL = REPO / "presets" / "ue_core.d.gs"
PORT = 8094
URL = f"http://127.0.0.1:{PORT}/"
OUT = ROOT / "test_screenshots" / "real_backend_large_graph_smoke"
OUT.mkdir(parents=True, exist_ok=True)
GENERATED_SOURCE = OUT / "real_backend_large_graph.gs"

NODE_COUNT = 1000
EVENT_COUNT = 20
NODES_PER_EVENT = NODE_COUNT // EVENT_COUNT
RENDER_BUDGET_MS = 30000
INTERACTION_BUDGET_MS = 5000
STATE_BUDGET_MS = 5000


def large_source():
    lines = [
        'import "ue_core.d.gs";',
        '[Comment("title", "Real backend thousand node smoke")]',
        "Graph RealBackendLarge {",
        "    in msg : FString;",
        "    in dur : float;",
        "",
    ]

    columns = 50
    for i in range(NODE_COUNT):
        type_name = "PrintString" if i % 2 == 0 else "Delay"
        x = 120 + (i % columns) * 260
        y = 120 + (i // columns) * 150
        lines.append(f"    [Position(X = {x}, Y = {y})]")
        lines.append(f"    {type_name} n{i}{{}};")

    for event_index in range(EVENT_COUNT):
        start = event_index * NODES_PER_EVENT
        end = start + NODES_PER_EVENT
        lines.append("")
        lines.append(f"    event E{event_index} {{")
        lines.append(f"        context.start(n{start}.enter);")
        for i in range(start, end - 1):
            pin = "exit" if i % 2 == 0 else "completed"
            lines.append(f"        n{i}.{pin}(n{i + 1}.enter);")
        for i in range(start, end, 10):
            if i % 2 == 0:
                lines.append(f"        link n{i}.message = msg;")
            else:
                lines.append(f"        link n{i}.duration = dur;")
        lines.append("    }")

    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def write_source():
    GENERATED_SOURCE.write_text(large_source(), encoding="utf-8")


def run_compile_check():
    if not GS_EXE.exists():
        raise RuntimeError(f"Missing executable: {GS_EXE}")
    if not CORE_DECL.exists():
        raise RuntimeError(f"Missing declaration fixture: {CORE_DECL}")
    start = time.perf_counter()
    result = subprocess.run(
        [str(GS_EXE), "compile", "-i", str(GENERATED_SOURCE), "-I", str(CORE_DECL)],
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=30,
    )
    compile_ms = round((time.perf_counter() - start) * 1000)
    if result.returncode != 0:
        raise RuntimeError("compile failed:\n" + result.stdout)
    return compile_ms, result.stdout


def wait_for_server():
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(URL + "api/state", timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("gs serve did not become ready")


def start_gs_serve():
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    proc = subprocess.Popen(
        [
            str(GS_EXE),
            "serve",
            "-p",
            str(PORT),
            "-I",
            str(CORE_DECL),
            "-i",
            str(GENERATED_SOURCE),
        ],
        cwd=REPO,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        creationflags=creationflags,
    )
    try:
        wait_for_server()
    except Exception:
        proc.terminate()
        raise
    return proc


def fetch_json(path):
    start = time.perf_counter()
    with urllib.request.urlopen(URL + path, timeout=10) as res:
        payload = json.loads(res.read().decode("utf-8"))
    elapsed_ms = round((time.perf_counter() - start) * 1000)
    return payload, elapsed_ms


def run_exec(command):
    payload = json.dumps({"command": command}).encode("utf-8")
    req = urllib.request.Request(
        URL + "api/exec",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=10) as res:
        return json.loads(res.read().decode("utf-8"))


def main():
    write_source()
    compile_ms, compile_output = run_compile_check()
    proc = start_gs_serve()
    results = []
    metrics = {
        "compile_ms": compile_ms,
    }
    try:
        state, state_ms = fetch_json("api/state")
        metrics["state_fetch_ms"] = state_ms
        graph = state["module"]["graphs"][0] if state["module"]["graphs"] else {}
        event = graph["events"][0] if graph.get("events") else {}

        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1600, "height": 1000})
            render_start = time.perf_counter()
            page.goto(URL, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_function(
                f"() => document.querySelectorAll('.node-card').length >= {NODE_COUNT}",
                timeout=RENDER_BUDGET_MS,
            )
            render_ms = round((time.perf_counter() - render_start) * 1000)
            metrics["initial_render_ms"] = render_ms
            metrics["node_cards"] = page.locator(".node-card").count()
            metrics["line_count"] = page.locator('[data-testid="sdk.workflow.canvas.line"]').count()

            select_start = time.perf_counter()
            page.locator(".node-card:has-text('n0')").first.click()
            page.wait_for_selector('[data-node-instance="n0"]', timeout=INTERACTION_BUDGET_MS)
            metrics["direct_select_ms"] = round((time.perf_counter() - select_start) * 1000)

            pan_start = time.perf_counter()
            page.mouse.move(800, 420)
            page.mouse.down()
            page.mouse.move(620, 360, steps=12)
            page.mouse.up()
            page.wait_for_timeout(250)
            metrics["pan_ms"] = round((time.perf_counter() - pan_start) * 1000)
            metrics["visible_after_pan"] = page.locator(".node-card").count()
            page.screenshot(path=OUT / "01_real_backend_large_graph.png", full_page=True)
            browser.close()

        exec_result = run_exec("annotate node n0 Position X=999 Y=777")
        metrics["annotate_ok"] = 1 if exec_result.get("ok") else 0
        state_after_annotate = exec_result.get("state", {})
        graph_after_annotate = state_after_annotate.get("module", {}).get("graphs", [{}])[0]
        node0 = next((node for node in graph_after_annotate.get("nodes", []) if node.get("instance") == "n0"), {})
        position = next((annotation for annotation in node0.get("annotations", []) if annotation.get("name") == "Position"), {})
        position_values = {arg.get("name"): arg.get("value") for arg in position.get("args", [])}

        expected_flows = NODE_COUNT - EVENT_COUNT
        results = [
            ("compile generated source", "Graphs: 1" in compile_output and compile_ms < RENDER_BUDGET_MS),
            ("state fetch within budget", state_ms < STATE_BUDGET_MS),
            ("server loaded graph", graph.get("name") == "RealBackendLarge"),
            ("server has 1000 nodes", len(graph.get("nodes", [])) == NODE_COUNT),
            ("server has 20 events", len(graph.get("events", [])) == EVENT_COUNT),
            ("server has expected first flow", event.get("flows", [])[:1] == [{
                "from_node": "context",
                "from_pin": "start",
                "to_node": "n0",
                "to_pin": "enter",
            }]),
            ("server annotate node replayed", metrics["annotate_ok"] == 1),
            ("server annotation changed state", position_values.get("X") == "999" and position_values.get("Y") == "777"),
            ("renders 1000 node cards", metrics["node_cards"] >= NODE_COUNT),
            ("renders expected flow lines", metrics["line_count"] >= expected_flows),
            ("initial render within smoke budget", metrics["initial_render_ms"] < RENDER_BUDGET_MS),
            ("direct node select within smoke budget", metrics["direct_select_ms"] < INTERACTION_BUDGET_MS),
            ("pan within smoke budget", metrics["pan_ms"] < INTERACTION_BUDGET_MS),
            ("canvas remains populated after pan", metrics["visible_after_pan"] >= NODE_COUNT),
        ]
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nReal backend large graph smoke")
    for key, value in metrics.items():
        print(f"  METRIC {key}={value}")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Generated source: {GENERATED_SOURCE}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
