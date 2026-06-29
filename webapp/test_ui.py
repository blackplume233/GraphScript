#!/usr/bin/env python3
"""Quick UI test for GraphScript web editor - navigates, screenshots, types in command log."""
import sys
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed. Run: pip install playwright && playwright install chromium")
    sys.exit(1)

URL = "http://localhost:5174/"
OUT_DIR = Path(__file__).parent / "test_screenshots"
OUT_DIR.mkdir(exist_ok=True)

def main():
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1280, "height": 900})
        page.goto(URL)
        page.wait_for_load_state("networkidle", timeout=10000)

        # 1. Initial full-page screenshot
        page.screenshot(path=OUT_DIR / "01_initial.png", full_page=True)
        print(f"Saved: {OUT_DIR / '01_initial.png'}")

        # 2. Screenshot of main layout (without full page)
        page.screenshot(path=OUT_DIR / "02_layout.png")
        print(f"Saved: {OUT_DIR / '02_layout.png'}")

        # 3. Type in command log input
        input_sel = 'input[placeholder*="add_node"]'
        page.wait_for_selector(input_sel, timeout=5000)
        page.fill(input_sel, "add_node PrintString ps1")
        page.screenshot(path=OUT_DIR / "03_command_typed.png")
        print(f"Saved: {OUT_DIR / '03_command_typed.png'}")

        # 4. Check for error banner (backend not running)
        error_el = page.locator("text=Cannot connect to backend")
        has_error = error_el.count() > 0
        print(f"Error banner visible: {has_error}")

        # 5. Check layout elements
        toolbar = page.locator("text=GraphScript").first
        palette = page.locator("text=Nodes").first
        console = page.locator("text=Console").first
        props = page.locator("text=Properties").first
        print(f"Toolbar: {toolbar.count() > 0}")
        print(f"Node palette: {palette.count() > 0}")
        print(f"Command log (Console): {console.count() > 0}")
        print(f"Properties panel: {props.count() > 0}")

        browser.close()
    print("Done.")

if __name__ == "__main__":
    main()
