#!/usr/bin/env python3
"""Verify GraphScript web editor fix - check rendering and console for Ambiguous match."""
import sys
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

URL = "http://localhost:5175/"
OUT = Path(__file__).parent / "test_screenshots"
OUT.mkdir(exist_ok=True)

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    page = browser.new_page(viewport={"width": 1400, "height": 900})
    console_msgs = []
    def on_console(msg):
        console_msgs.append({"type": msg.type, "text": msg.text})
    page.on("console", on_console)
    page.goto(URL, wait_until="networkidle", timeout=20000)
    page.wait_for_timeout(2000)

    # Screenshots
    page.screenshot(path=OUT / "verify_full.png", full_page=True)
    page.screenshot(path=OUT / "verify_viewport.png")

    # Layout checks
    toolbar = page.locator("text=GraphScript").count() > 0
    left_panel = page.locator("text=NODES").count() > 0
    canvas = page.locator(".blueprint-grid, [class*='flowgram'], [class*='blueprint']").count() > 0
    right_panel = page.locator("text=Graph").or_(page.locator("text=Node")).count() > 0
    console_panel = page.locator("text=Console").count() > 0
    root_has_content = len(page.locator("#root").inner_html()) > 100

    # Status
    live = page.locator("text=Live").count() > 0
    offline = page.locator("text=Offline").count() > 0
    status = "Live" if live else ("Offline" if offline else "Unknown")

    # Console errors
    errors = [m for m in console_msgs if m["type"] == "error"]
    ambiguous = [m for m in console_msgs if "Ambiguous match" in m.get("text", "")]
    warnings = [m for m in console_msgs if m["type"] == "warning"]

    browser.close()

# Report
print("=" * 60)
print("GraphScript Web Editor - Verify Fix Report")
print("=" * 60)
print("\n1. Page render:")
print(f"   #root has content: {root_has_content}")
print(f"   Toolbar (GraphScript): {toolbar}")
print(f"   Left panel (NODES): {left_panel}")
print(f"   Center canvas: {canvas}")
print(f"   Right panel (Graph/Node): {right_panel}")
print(f"   Bottom command log (Console): {console_panel}")
print(f"\n2. Backend status: {status}")
print(f"\n3. Console errors: {len(errors)}")
for e in errors[:3]:
    print(f"   - {e['text'][:200]}")
print(f"\n4. 'Ambiguous match' in console: {len(ambiguous)}")
for a in ambiguous[:2]:
    print(f"   - {a['text'][:300]}")
print(f"\n5. Warnings: {len(warnings)}")
for w in warnings[:2]:
    print(f"   - {w['text'][:200]}")
print(f"\nScreenshots: {OUT / 'verify_full.png'}, {OUT / 'verify_viewport.png'}")
print("=" * 60)
