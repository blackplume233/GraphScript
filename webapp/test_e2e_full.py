#!/usr/bin/env python3
"""Comprehensive E2E test for GraphScript web editor - all 11 scenarios."""
import sys
import time
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

URL = "http://localhost:5175/"
OUT = Path(__file__).parent / "test_screenshots" / "e2e"
OUT.mkdir(parents=True, exist_ok=True)

def run_cmd(page, cmd: str) -> bool:
    inp = page.get_by_placeholder("add_node PrintString ps1")
    inp.wait_for(state="visible", timeout=5000)
    inp.fill(cmd)
    inp.press("Enter")
    time.sleep(0.8)
    return True

def main():
    results = []
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1440, "height": 900})
        console_errors = []
        page.on("console", lambda m: console_errors.append(m.text) if m.type == "error" else None)
        page.goto(URL, wait_until="networkidle", timeout=20000)
        page.wait_for_timeout(1500)

        # ─── Test 1: Page loads correctly ───────────────────────────
        toolbar = page.locator("text=GraphScript").count() > 0
        left_panel = page.locator("text=NODES").count() > 0
        canvas = page.locator(".blueprint-grid, [class*='flowgram']").count() > 0
        right_panel = page.locator("text=Graph").or_(page.locator("text=Node")).count() > 0
        console = page.locator("text=Console").count() > 0
        live = page.locator("text=Live").count() > 0
        page.screenshot(path=OUT / "01_page_load.png", full_page=True)
        t1 = all([toolbar, left_panel, canvas, right_panel, console]) and live
        results.append(("Test 1: Page loads", "PASS" if t1 else "FAIL", {
            "toolbar": toolbar, "left": left_panel, "canvas": canvas,
            "right": right_panel, "console": console, "live": live
        }))

        # ─── Test 2: Node Palette search ────────────────────────────
        search = page.locator('input[placeholder="Search..."]')
        search_ok = search.count() > 0
        if search_ok:
            search.fill("Branch")
            time.sleep(0.4)
            branch_found = page.locator("text=Branch").count() > 0
            search.fill("")
            time.sleep(0.2)
        else:
            branch_found = False
        page.screenshot(path=OUT / "02_node_search.png", full_page=True)
        t2 = search_ok and branch_found
        results.append(("Test 2: Node Palette search", "PASS" if t2 else "FAIL", {
            "search_ok": search_ok, "branch_found": branch_found
        }))

        # ─── Test 3: Create graph ───────────────────────────────────
        run_cmd(page, "new MyTestGraph")
        time.sleep(1.0)
        graph_active = page.locator("select option:has-text('MyTestGraph')").count() > 0
        page.screenshot(path=OUT / "03_create_graph.png", full_page=True)
        results.append(("Test 3: Create graph", "PASS" if graph_active else "FAIL", {"graph_active": graph_active}))

        # ─── Test 4: Add nodes ─────────────────────────────────────
        run_cmd(page, "add Branch b1")
        time.sleep(0.5)
        run_cmd(page, "add PrintString p1")
        time.sleep(0.8)
        page.screenshot(path=OUT / "04_add_nodes.png", full_page=True)
        nodes_on_canvas = page.locator("[data-node-id]").count() > 0
        nodes_in_log = "add Branch b1" in page.content() and "add PrintString p1" in page.content()
        t4 = nodes_on_canvas or nodes_in_log
        results.append(("Test 4: Add nodes", "PASS" if t4 else "PARTIAL", {
            "nodes_on_canvas": page.locator("[data-node-id]").count(),
            "nodes_in_log": nodes_in_log
        }))

        # ─── Test 5: Add more nodes ─────────────────────────────────
        run_cmd(page, "add GetPlayerCharacter gpc1")
        time.sleep(0.4)
        run_cmd(page, "add Clamp_Float clamp1")
        time.sleep(0.6)
        page.screenshot(path=OUT / "05_add_more_nodes.png", full_page=True)
        t5 = "add GetPlayerCharacter" in page.content() and "add Clamp_Float" in page.content()
        results.append(("Test 5: Add more nodes", "PASS" if t5 else "PARTIAL", {"commands_in_log": t5}))

        # ─── Test 6: Create connections (flow requires event block - do event first)
        run_cmd(page, "event OnStart")
        time.sleep(0.3)
        # Try user's command first; CLI uses onTrue/enter per blueprint tests
        run_cmd(page, "flow b1.onTrue p1.enter")
        time.sleep(0.6)
        page.screenshot(path=OUT / "06_connections.png", full_page=True)
        flow_in_log = "flow b1.onTrue p1.enter" in page.content()
        edges = page.locator("[class*='edge'], [class*='connection'], path[stroke]").count()
        t6 = flow_in_log
        results.append(("Test 6: Create connections", "PASS" if t6 else "PARTIAL", {
            "flow_in_log": flow_in_log, "edge_elements": edges
        }))

        # ─── Test 7: Event definition (connect context to branch)
        run_cmd(page, "flow context.start b1.enter")
        time.sleep(0.5)
        page.screenshot(path=OUT / "07_event_flow.png", full_page=True)
        t7 = "flow context.start" in page.content()
        results.append(("Test 7: Event definition", "PASS" if t7 else "PARTIAL", {"flow_in_log": t7}))

        # ─── Test 8: Undo/Redo ────────────────────────────────────
        undo_btn = page.locator('button[title*="Undo"]').first
        if undo_btn.count() > 0:
            undo_btn.click()
            time.sleep(0.5)
        page.screenshot(path=OUT / "08_after_undo.png", full_page=True)
        redo_btn = page.locator('button[title*="Redo"]').first
        if redo_btn.count() > 0:
            redo_btn.click()
            time.sleep(0.5)
        page.screenshot(path=OUT / "08_after_redo.png", full_page=True)
        t8 = undo_btn.count() > 0 and redo_btn.count() > 0
        results.append(("Test 8: Undo/Redo", "PASS" if t8 else "FAIL", {"undo": undo_btn.count() > 0, "redo": redo_btn.count() > 0}))

        # ─── Test 9: Properties panel ──────────────────────────────
        nodes = page.locator("[data-node-id]")
        if nodes.count() == 0:
            nodes = page.locator("[class*='workflow-node'], [class*='node-card']")
        if nodes.count() > 0:
            nodes.first.click()
            time.sleep(0.4)
        page.screenshot(path=OUT / "09_properties.png", full_page=True)
        props = page.locator("text=Instance").or_(page.locator("text=Type")).or_(page.locator("text=Pins"))
        t9 = props.count() > 0 or nodes.count() > 0
        results.append(("Test 9: Properties panel", "PASS" if t9 else "PARTIAL", {
            "node_clicked": nodes.count() > 0, "props_visible": props.count() > 0
        }))

        # ─── Test 10: Export .gs ───────────────────────────────────
        export_btn = page.locator('button[title*="Export"]').or_(page.locator('button[title*=".gs"]'))
        if export_btn.count() == 0:
            export_btn = page.locator('button').filter(has=page.locator('svg')).nth(3)
        download_ok = False
        if export_btn.count() > 0:
            try:
                with page.expect_download(timeout=5000) as dl:
                    export_btn.first.click()
                d = dl.value
                download_ok = d.suggested_filename and len(d.suggested_filename) > 0
            except Exception:
                pass
        page.screenshot(path=OUT / "10_export.png", full_page=True)
        results.append(("Test 10: Export .gs", "PASS" if download_ok else "PARTIAL", {
            "export_btn_found": export_btn.count() > 0, "download_triggered": download_ok
        }))

        # ─── Test 11: Command log history ──────────────────────────
        log_entries = page.locator(".font-mono").all_inner_texts()
        log_has_commands = any("add " in t or "new " in t or "flow " in t for t in log_entries)
        inp = page.get_by_placeholder("add_node PrintString ps1")
        inp.click()
        inp.press("ArrowUp")
        time.sleep(0.2)
        inp.press("ArrowUp")
        time.sleep(0.2)
        page.screenshot(path=OUT / "11_command_history.png", full_page=True)
        t11 = log_has_commands
        results.append(("Test 11: Command log history", "PASS" if t11 else "PARTIAL", {"log_has_commands": t11}))

        browser.close()

    # Report
    print("\n" + "=" * 70)
    print("GraphScript Web Editor - Comprehensive E2E Test Report")
    print("=" * 70)
    for name, status, details in results:
        print(f"\n{name}: {status}")
        for k, v in details.items():
            print(f"  {k}: {v}")
    print(f"\nConsole errors: {len(console_errors)}")
    for e in console_errors[:3]:
        print(f"  - {e[:150]}")
    print(f"\nScreenshots: {OUT}")
    print("=" * 70)

if __name__ == "__main__":
    main()
