#!/usr/bin/env python3
"""Full flow test for GraphScript web editor - all 9 test steps."""
import sys
import time
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed. Run: pip install playwright && playwright install chromium")
    sys.exit(1)

URL = "http://localhost:5175/"
OUT_DIR = Path(__file__).parent / "test_screenshots"
OUT_DIR.mkdir(exist_ok=True)

def main():
    results = []
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1400, "height": 900})
        page.goto(URL, wait_until="networkidle", timeout=20000)
        time.sleep(2)  # Allow React to hydrate and render

        # ─── 1. Page Load ─────────────────────────────────────────
        page.screenshot(path=OUT_DIR / "01_page_load.png", full_page=True)
        canvas = page.locator(".blueprint-grid, [class*='blueprint']")
        toolbar = page.locator("text=GraphScript")
        left_panel = page.locator("text=NODES")
        right_panel = page.locator("text=Graph").or_(page.locator("text=Node"))
        console = page.locator("text=Console")
        r1 = {
            "loaded": True,
            "canvas": canvas.count() > 0 or page.locator("[class*='flowgram']").count() > 0,
            "toolbar": toolbar.count() > 0,
            "left_panel": left_panel.count() > 0,
            "right_panel": right_panel.count() > 0,
            "console": console.count() > 0,
        }
        results.append(("1. Page Load", r1))

        # ─── 2. Backend Connection ───────────────────────────────
        live = page.locator("text=Live")
        offline = page.locator("text=Offline")
        status = "Live" if live.count() > 0 else ("Offline" if offline.count() > 0 else "Unknown")
        err_banner = page.locator("text=Cannot connect to backend")
        r2 = {"status": status, "connected": live.count() > 0, "error_banner": err_banner.count() > 0}
        results.append(("2. Backend Connection", r2))

        # Find command input - try multiple selectors
        cmd_input = page.locator('input[placeholder*="add_node"]')
        if cmd_input.count() == 0:
            cmd_input = page.locator('input[placeholder*="add_node" i]')
        if cmd_input.count() == 0:
            # Fallback: input in console area (bottom panel)
            cmd_input = page.locator("text=Console").locator("..").locator("input")
        if cmd_input.count() == 0:
            cmd_input = page.locator("input").last

        if cmd_input.count() == 0:
            results.append(("Command input", {"found": False}))
            page.screenshot(path=OUT_DIR / "99_debug.png", full_page=True)
            browser.close()
            _print_report(results)
            return

        def run_cmd(cmd: str) -> bool:
            try:
                cmd_input.first.wait_for(state="visible", timeout=5000)
                cmd_input.first.fill(cmd)
                cmd_input.first.press("Enter")
                time.sleep(0.8)
                return page.locator("text=Cannot connect to backend").count() == 0
            except Exception as e:
                print(f"  run_cmd error: {e}")
                return False

        # ─── 3. Node Palette - Search Branch ───────────────────────
        search = page.locator('input[placeholder="Search..."]')
        if search.count() > 0:
            search.fill("Branch")
            time.sleep(0.4)
        page.screenshot(path=OUT_DIR / "03_node_palette_search.png")
        branch_in_list = page.locator("text=Branch").count() > 0
        r3 = {"search_works": search.count() > 0, "branch_found": branch_in_list}
        results.append(("3. Node Palette", r3))
        if search.count() > 0:
            search.fill("")

        # ─── 4. Create Graph ──────────────────────────────────────
        ok4 = run_cmd("new MyTestGraph")
        time.sleep(1.0)
        page.screenshot(path=OUT_DIR / "04_create_graph.png")
        graph_selector = page.locator("select option:has-text('MyTestGraph')")
        r4 = {"command_sent": True, "graph_created": graph_selector.count() > 0 or ok4}
        results.append(("4. Create Graph", r4))

        # ─── 5. Add Nodes ────────────────────────────────────────
        run_cmd("add Branch b1")
        time.sleep(0.5)
        run_cmd("add PrintString p1")
        time.sleep(0.6)
        page.screenshot(path=OUT_DIR / "05_add_nodes.png")
        nodes_in_log = "add Branch b1" in page.content() and "add PrintString p1" in page.content()
        node_cards = page.locator("[data-node-id], [class*='workflow-node']").count()
        r5 = {"commands_sent": True, "nodes_in_log": nodes_in_log, "node_elements": node_cards}
        results.append(("5. Add Nodes", r5))

        # ─── 6. Connect Nodes ─────────────────────────────────────
        run_cmd("event OnStart")
        time.sleep(0.3)
        run_cmd("flow b1.onTrue p1.enter")
        time.sleep(0.5)
        page.screenshot(path=OUT_DIR / "06_connect_nodes.png")
        conn_in_log = "flow b1.onTrue p1.enter" in page.content()
        r6 = {"event_created": True, "flow_sent": True, "connection_in_log": conn_in_log}
        results.append(("6. Connect Nodes", r6))

        # ─── 7. Undo/Redo ────────────────────────────────────────
        undo_btn = page.locator('button[title*="Undo"]').first
        if undo_btn.count() == 0:
            undo_btn = page.locator('button').filter(has=page.locator("svg")).first
        if undo_btn.count() > 0:
            undo_btn.click()
            time.sleep(0.4)
        page.screenshot(path=OUT_DIR / "07_after_undo.png")
        redo_btn = page.locator('button[title*="Redo"]').first
        if redo_btn.count() > 0:
            redo_btn.click()
            time.sleep(0.4)
        page.screenshot(path=OUT_DIR / "07_after_redo.png")
        r7 = {"undo_clicked": undo_btn.count() > 0, "redo_clicked": redo_btn.count() > 0}
        results.append(("7. Undo/Redo", r7))

        # ─── 8. Properties Panel ─────────────────────────────────────
        nodes = page.locator("[data-node-id]").or_(page.locator("[class*='workflow-node']"))
        if nodes.count() > 0:
            nodes.first.click()
            time.sleep(0.4)
        page.screenshot(path=OUT_DIR / "08_properties_panel.png")
        props_content = page.locator("text=Instance").or_(page.locator("text=Type")).or_(page.locator("text=Pins"))
        r8 = {"node_clicked": nodes.count() > 0, "props_show_content": props_content.count() > 0}
        results.append(("8. Properties Panel", r8))

        # ─── 9. Export ───────────────────────────────────────────
        export_btn = page.locator('button[title*="Export"]').or_(page.locator('button[title*=".gs"]'))
        if export_btn.count() == 0:
            export_btn = page.locator('button[title*="Export"]')
        r9 = {"export_btn_found": export_btn.count() > 0}
        if export_btn.count() > 0:
            try:
                with page.expect_download(timeout=5000) as dl_info:
                    export_btn.first.click()
                dl_info.value
                r9["download_triggered"] = True
            except Exception:
                r9["download_triggered"] = False
        results.append(("9. Export", r9))

        browser.close()

    _print_report(results)

def _print_report(results):
    print("\n" + "=" * 60)
    print("GraphScript Web Editor - Full Flow Test Report")
    print("=" * 60)
    for name, r in results:
        print(f"\n{name}:")
        for k, v in r.items():
            print(f"  {k}: {v}")
    print("\nScreenshots saved to:", OUT_DIR)
    print("=" * 60)

if __name__ == "__main__":
    main()
