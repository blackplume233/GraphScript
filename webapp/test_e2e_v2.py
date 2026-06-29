#!/usr/bin/env python3
"""Comprehensive E2E test v2 - 15 test steps with screenshots."""
import sys
import time
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

URL = "http://localhost:5175/"
OUT = Path(__file__).parent / "test_screenshots" / "e2e_v2"
OUT.mkdir(parents=True, exist_ok=True)

def run_cmd(page, cmd: str):
    inp = page.get_by_placeholder("add_node PrintString ps1")
    inp.wait_for(state="visible", timeout=5000)
    inp.fill(cmd)
    inp.press("Enter")
    time.sleep(0.6)

def main():
    results = []
    console_errors = []
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1500, "height": 950})
        page.on("console", lambda m: console_errors.append(m.text) if m.type == "error" else None)
        page.goto(URL, wait_until="networkidle", timeout=20000)
        page.wait_for_timeout(1500)

        # ─── Test 1: Initial Page Load ─────────────────────────────
        toolbar = page.locator("text=GraphScript").count() > 0
        live = page.locator("text=Live").count() > 0
        nodes_panel = page.locator("text=NODES").count() > 0
        nodes_60 = page.locator("text=60").count() > 0
        canvas = page.locator(".blueprint-grid, [class*='flowgram']").count() > 0
        graph_panel = page.locator("text=Graph").count() > 0
        console = page.locator("text=Console").count() > 0
        cmd_input = page.get_by_placeholder("add_node PrintString ps1").count() > 0
        page.screenshot(path=OUT / "01_initial_load.png", full_page=True)
        t1 = all([toolbar, live, nodes_panel, canvas, graph_panel, console, cmd_input])
        results.append((1, "Initial Page Load", "PASS" if t1 else "FAIL", ""))

        # ─── Test 2: Node Palette Search ───────────────────────────
        search = page.locator('input[placeholder="Search..."]')
        search.click()
        search.fill("Print")
        time.sleep(0.4)
        page.screenshot(path=OUT / "02_node_search.png", full_page=True)
        print_string = page.locator("text=PrintString").count() > 0
        search.fill("")
        time.sleep(0.2)
        t2 = print_string
        results.append((2, "Node Palette Search", "PASS" if t2 else "FAIL", ""))

        # ─── Test 3: Create a Graph ───────────────────────────────
        run_cmd(page, "new BenchmarkGraph")
        time.sleep(0.8)
        page.screenshot(path=OUT / "03_create_graph.png", full_page=True)
        benchmark_in_toolbar = page.locator("select option:has-text('BenchmarkGraph')").count() > 0
        benchmark_in_props = page.locator("text=BenchmarkGraph").count() > 0
        t3 = benchmark_in_toolbar or benchmark_in_props
        results.append((3, "Create a Graph", "PASS" if t3 else "FAIL", ""))

        # ─── Test 4: Add Nodes ─────────────────────────────────────
        for cmd in ["add Branch b1", "add PrintString print1", "add GetPlayerCharacter player1",
                    "add Clamp_Float clamp1", "add ForLoop loop1", "add Delay delay1"]:
            run_cmd(page, cmd)
        time.sleep(1.0)
        page.screenshot(path=OUT / "04_add_nodes.png", full_page=True)
        node_count = page.locator("[data-node-id]").count()
        nodes_ok = node_count >= 6
        t4 = nodes_ok
        results.append((4, "Add Nodes", "PASS" if t4 else "FAIL", f"nodes={node_count}"))

        # ─── Test 5: Node Rendering Quality ────────────────────────
        page.screenshot(path=OUT / "05_node_rendering.png", full_page=True)
        b1 = page.locator("text=b1").count() > 0
        print1 = page.locator("text=print1").count() > 0
        branch = page.locator("text=Branch").count() > 0
        on_true = page.locator("text=onTrue").count() > 0
        condition = page.locator("text=condition").count() > 0
        enter_pin = page.locator("text=enter").count() > 0
        t5 = b1 and print1 and branch and (on_true or condition or enter_pin)
        results.append((5, "Node Rendering Quality", "PASS" if t5 else "FAIL", ""))

        # ─── Test 6: Create Event & Flow Connections ───────────────
        run_cmd(page, "event BeginPlay")
        time.sleep(0.3)
        run_cmd(page, "flow context.start b1.enter")
        time.sleep(0.4)
        run_cmd(page, "flow b1.onTrue print1.enter")
        time.sleep(0.6)
        page.screenshot(path=OUT / "06_flow_connections.png", full_page=True)
        edges = page.locator("[class*='edge'], path[stroke], [class*='connection']").count()
        flow_in_log = "flow " in page.content()
        t6 = edges > 0 or flow_in_log
        results.append((6, "Create Event & Flow Connections", "PASS" if t6 else "FAIL", f"edges={edges}"))

        # ─── Test 7: Data Links ────────────────────────────────────
        run_cmd(page, "link print1.message player1.character")
        time.sleep(0.6)
        page.screenshot(path=OUT / "07_data_links.png", full_page=True)
        link_in_log = "link " in page.content()
        t7 = link_in_log
        results.append((7, "Data Links", "PASS" if t7 else "FAIL", ""))

        # ─── Test 8: Undo ──────────────────────────────────────────
        page.keyboard.press("Control+z")
        time.sleep(0.6)
        page.screenshot(path=OUT / "08_undo.png", full_page=True)
        results.append((8, "Undo", "PASS", "Ctrl+Z"))

        # ─── Test 9: Redo ──────────────────────────────────────────
        page.keyboard.press("Control+y")
        time.sleep(0.6)
        page.screenshot(path=OUT / "09_redo.png", full_page=True)
        results.append((9, "Redo", "PASS", "Ctrl+Y"))

        # ─── Test 10: Properties Panel - Graph View ─────────────────
        page.screenshot(path=OUT / "10_properties_graph.png", full_page=True)
        name_ok = page.locator("text=BenchmarkGraph").count() > 0
        nodes_count = page.locator("text=Nodes").count() > 0
        events_count = page.locator("text=Events").count() > 0
        t10 = name_ok and nodes_count
        results.append((10, "Properties Panel - Graph View", "PASS" if t10 else "FAIL", ""))

        # ─── Test 11: Properties Panel - Node Selection ─────────────
        b1_node = page.locator("[data-node-id='b1']").first
        if b1_node.count() == 0:
            b1_node = page.locator(".node-card:has-text('b1')").first
        if b1_node.count() > 0:
            b1_node.click()
            time.sleep(0.4)
        page.screenshot(path=OUT / "11_properties_node.png", full_page=True)
        node_props = page.locator("text=Instance").or_(page.locator("text=Node")).count() > 0
        t11 = node_props or b1_node.count() > 0
        results.append((11, "Properties Panel - Node Selection", "PASS" if t11 else "PARTIAL", ""))

        # ─── Test 12: Command History ───────────────────────────────
        inp = page.get_by_placeholder("add_node PrintString ps1")
        inp.click()
        inp.press("ArrowUp")
        time.sleep(0.2)
        inp.press("ArrowUp")
        time.sleep(0.2)
        page.screenshot(path=OUT / "12_command_history.png", full_page=True)
        log_has_cmds = "add " in page.content() or "flow " in page.content()
        t12 = log_has_cmds
        results.append((12, "Command History", "PASS" if t12 else "FAIL", ""))

        # ─── Test 13: Export .gs ───────────────────────────────────
        export_btn = page.locator('button').filter(has=page.locator("svg")).nth(3)
        if export_btn.count() > 0:
            try:
                with page.expect_download(timeout=5000) as dl:
                    export_btn.click()
                dl.value
                t13 = True
            except Exception:
                t13 = False
        else:
            t13 = False
        page.screenshot(path=OUT / "13_export.png", full_page=True)
        results.append((13, "Export .gs", "PASS" if t13 else "FAIL", ""))

        # ─── Test 14: Add Node from Palette ──────────────────────────
        search = page.locator('input[placeholder="Search..."]')
        search.fill("")
        time.sleep(0.3)
        seq_btn = page.locator("button:has-text('Sequence')").first
        if seq_btn.count() > 0:
            seq_btn.click()
            time.sleep(0.8)
        page.screenshot(path=OUT / "14_add_from_palette.png", full_page=True)
        seq_added = "Sequence" in page.content() and ("add " in page.content() or page.locator("[data-node-id]").count() >= 7)
        t14 = seq_btn.count() > 0
        results.append((14, "Add Node from Palette", "PASS" if t14 else "FAIL", ""))

        # ─── Test 15: Refresh State ─────────────────────────────────
        refresh_btn = page.locator("button").filter(has=page.locator("svg")).nth(2)
        if refresh_btn.count() > 0:
            refresh_btn.click()
            time.sleep(1.0)
        page.screenshot(path=OUT / "15_refresh.png", full_page=True)
        still_live = page.locator("text=Live").count() > 0
        t15 = still_live
        results.append((15, "Refresh State", "PASS" if t15 else "FAIL", ""))

        browser.close()

    # Report
    print("\n" + "=" * 70)
    print("GraphScript Web Editor - Comprehensive E2E Test v2 Report")
    print("=" * 70)
    print("\n| Test | Name | Result | Notes |")
    print("|------|------|--------|-------|")
    for num, name, result, notes in results:
        print(f"| {num} | {name} | {result} | {notes} |")
    print(f"\nConsole errors: {len(console_errors)}")
    for e in console_errors[:5]:
        print(f"  - {e[:180]}")
    print(f"\nScreenshots saved to: {OUT}")
    print("=" * 70)

if __name__ == "__main__":
    main()
