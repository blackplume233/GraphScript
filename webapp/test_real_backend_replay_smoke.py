#!/usr/bin/env python3
"""Smoke test for Web UI command replay against a real gs serve backend."""
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

try:
    from playwright.sync_api import TimeoutError as PlaywrightTimeoutError
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
GS_EXE_CANDIDATES = [
    REPO / "build" / "Release" / "gs.exe",
    REPO / "build-codex" / "Release" / "gs.exe",
]
DECL = REPO / "tests" / "fixtures" / "mixed_declarations.d.gs"
PORT = 8093
URL = f"http://127.0.0.1:{PORT}/"
OUT = ROOT / "test_screenshots" / "real_backend_replay_smoke"
OUT.mkdir(parents=True, exist_ok=True)


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
    gs_exe = next((candidate for candidate in GS_EXE_CANDIDATES if candidate.exists()), None)
    if gs_exe is None:
        raise RuntimeError(
            "Missing executable; expected one of: " +
            ", ".join(str(candidate) for candidate in GS_EXE_CANDIDATES)
        )
    if not DECL.exists():
        raise RuntimeError(f"Missing declaration fixture: {DECL}")

    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    proc = subprocess.Popen(
        [str(gs_exe), "serve", "-p", str(PORT), "-I", str(DECL)],
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
    with urllib.request.urlopen(URL + path, timeout=5) as res:
        return json.loads(res.read().decode("utf-8"))


def fetch_text(path):
    with urllib.request.urlopen(URL + path, timeout=5) as res:
        return res.read().decode("utf-8")


def graph_by_name_or_active(state, graph_name):
    graphs = state.get("module", {}).get("graphs", [])
    for graph in graphs:
        if graph.get("name") == graph_name:
            return graph
    active = state.get("active_graph", -1)
    if isinstance(active, int) and 0 <= active < len(graphs):
        return graphs[active]
    return graphs[0] if graphs else {}


def run_console_command(page, command):
    prompt = page.get_by_placeholder("add_node PrintString ps1")
    try:
        prompt.fill(command, timeout=1000)
    except PlaywrightTimeoutError:
        console_tab = page.locator(".dv-tab").filter(has_text="Console").first
        try:
            console_tab.click(timeout=5000)
        except PlaywrightTimeoutError:
            page.get_by_text("Console", exact=True).first.click(timeout=5000)
        prompt.fill(command)
    prompt.press("Enter")
    deadline = time.time() + 5
    while time.time() < deadline:
        state = fetch_json("api/state")
        if command in state.get("command_log", []):
            return
        page.wait_for_timeout(100)
    raise RuntimeError(f"Command did not reach backend log: {command}")


def main():
    proc = start_gs_serve()
    results = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1500, "height": 950})
            page.goto(URL, wait_until="networkidle", timeout=20000)
            page.wait_for_selector('input[placeholder="graph"]', timeout=10000)
            page.screenshot(path=OUT / "01_initial_real_backend.png", full_page=True)

            commands = [
                "create_graph SmokeGraph",
                "param in tag GameplayTag",
                "event BeginPlay",
                "add_node BranchOnTag branch",
                "add_node PlayMontage montage",
                'set_init montage montage "\\"/Game/Montage\\""',
                "flow branch.matched montage.play",
                "link branch.tag tag",
            ]
            for command in commands:
                run_console_command(page, command)

            state_after_replay = fetch_json("api/state")
            graph_after_replay = graph_by_name_or_active(state_after_replay, "SmokeGraph")
            replayed_nodes = {node["instance"] for node in graph_after_replay.get("nodes", [])}
            if not {"branch", "montage"}.issubset(replayed_nodes):
                raise RuntimeError(
                    "Backend command replay did not create expected nodes; "
                    f"nodes={sorted(replayed_nodes)}, "
                    f"commands={state_after_replay.get('command_log', [])[-len(commands):]}"
                )

            page.wait_for_selector(".node-card:has-text('branch')", timeout=5000)
            page.wait_for_selector(".node-card:has-text('montage')", timeout=5000)
            page.wait_for_selector(
                '[data-line-id="branch_exec-out-matched-montage_exec-in-play"]',
                state="attached",
                timeout=5000,
            )
            page.screenshot(path=OUT / "02_real_backend_graph_created.png", full_page=True)

            state = fetch_json("api/state")
            emitted = fetch_text("api/emit")
            graph = graph_by_name_or_active(state, "SmokeGraph")
            event = graph["events"][0] if graph.get("events") else {}
            flow = event.get("flows", [{}])[0] if event.get("flows") else {}
            link = event.get("links", [{}])[0] if event.get("links") else {}
            montage = next((node for node in graph.get("nodes", []) if node.get("instance") == "montage"), {})
            montage_field = next(
                (field for field in montage.get("initializer_fields", []) if field.get("name") == "montage"),
                {},
            )
            line_ids = page.locator('[data-testid="sdk.workflow.canvas.line"]').evaluate_all(
                "els => els.map(e => e.getAttribute('data-line-id')).sort()"
            )

            results = [
                ("server loaded declarations", len(state["types"]) >= 2),
                ("graph created in real state", graph.get("name") == "SmokeGraph"),
                ("parameter replayed", any(param["name"] == "tag" for param in graph.get("parameters", []))),
                ("nodes replayed", {node["instance"] for node in graph.get("nodes", [])} >= {"branch", "montage"}),
                ("quoted initializer replayed", montage_field.get("value") == '"/Game/Montage"'),
                ("event replayed", event.get("name") == "BeginPlay"),
                ("flow replayed", flow.get("from_node") == "branch" and flow.get("from_pin") == "matched" and flow.get("to_node") == "montage" and flow.get("to_pin") == "play"),
                ("link replayed", link.get("target_node") == "branch" and link.get("target_pin") == "tag" and link.get("source_node") == "tag" and link.get("source_pin") == ""),
                ("command log persisted", state["command_log"][-len(commands):] == commands),
                ("command log visible", all(page.locator(f"text={command}").count() > 0 for command in commands)),
                ("visual flow line rendered", "branch_exec-out-matched-montage_exec-in-play" in line_ids),
                ("emitted source has flow", "connect(branch.matched, montage.play);" in emitted),
                ("emitted source has link", "bind(tag, branch.tag);" in emitted),
                ("emitted source has quoted initializer", 'montage: "/Game/Montage";' in emitted),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nReal backend replay smoke")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
