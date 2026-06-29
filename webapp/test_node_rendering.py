#!/usr/bin/env python3
"""Test node rendering - verify nodes show type names and pins instead of '?'."""
import sys
import time
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

URL = "http://localhost:5175/"
OUT = Path(__file__).parent / "test_screenshots" / "node_render"
OUT.mkdir(parents=True, exist_ok=True)

def run_cmd(page, cmd: str):
    inp = page.get_by_placeholder("add_node PrintString ps1")
    inp.wait_for(state="visible", timeout=5000)
    inp.fill(cmd)
    inp.press("Enter")
    time.sleep(0.7)

def main():
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1440, "height": 900})
        console_errors = []
        page.on("console", lambda m: console_errors.append({"type": m.type, "text": m.text}) if m.type == "error" else None)
        page.goto(URL, wait_until="networkidle", timeout=20000)
        page.wait_for_timeout(1500)

        # Create graph and add nodes
        run_cmd(page, "new TestGraph")
        time.sleep(0.8)
        run_cmd(page, "add Branch b1")
        time.sleep(0.6)
        run_cmd(page, "add PrintString p1")
        time.sleep(0.6)
        run_cmd(page, "add Clamp_Float clamp1")
        time.sleep(0.8)

        # Screenshots
        page.screenshot(path=OUT / "canvas_full.png", full_page=True)
        # Canvas area only (center)
        canvas = page.locator(".blueprint-grid").first
        if canvas.count() > 0:
            canvas.screenshot(path=OUT / "canvas_area.png")
        page.screenshot(path=OUT / "canvas_viewport.png")

        # Check node content
        question_marks = page.locator("text=?").count()
        b1_visible = page.locator("text=b1").count() > 0
        p1_visible = page.locator("text=p1").count() > 0
        clamp1_visible = page.locator("text=clamp1").count() > 0
        branch_visible = page.locator("text=Branch").count() > 0
        print_string_visible = page.locator("text=PrintString").count() > 0
        clamp_float_visible = page.locator("text=Clamp_Float").count() > 0
        pin_names = page.locator("text=condition").or_(page.locator("text=onTrue")).or_(page.locator("text=enter")).or_(page.locator("text=message")).count() > 0

        browser.close()

    # Report
    print("\n" + "=" * 60)
    print("Node Rendering Test Report")
    print("=" * 60)
    print(f"\nInstance names visible:")
    print(f"  b1: {b1_visible}")
    print(f"  p1: {p1_visible}")
    print(f"  clamp1: {clamp1_visible}")
    print(f"\nType names visible:")
    print(f"  Branch: {branch_visible}")
    print(f"  PrintString: {print_string_visible}")
    print(f"  Clamp_Float: {clamp_float_visible}")
    print(f"\nPin names visible: {pin_names}")
    print(f"Question marks ('?') on canvas: {question_marks}")
    print(f"\nConsole errors: {len(console_errors)}")
    for e in console_errors[:5]:
        print(f"  - {e['text'][:200]}")
    print(f"\nScreenshots: {OUT}")
    print("=" * 60)

if __name__ == "__main__":
    main()
