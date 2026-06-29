"""Debug: inspect page at localhost:5175"""
import sys
from pathlib import Path
from playwright.sync_api import sync_playwright

OUT = Path("test_screenshots")
OUT.mkdir(exist_ok=True)

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    page = browser.new_page(viewport={"width": 1400, "height": 900})
    errors = []
    all_msgs = []
    def on_console(msg):
        all_msgs.append((msg.type, msg.text))
        if msg.type == "error": errors.append(msg.text)
    page.on("console", on_console)
    page.goto("http://localhost:5174/", wait_until="networkidle", timeout=15000)
    # Wait for React to render - look for GraphScript text in the UI
    try:
        page.wait_for_selector("text=GraphScript", timeout=10000)
        print("GraphScript text found in DOM")
    except Exception as e:
        print("Timeout waiting for GraphScript:", e)
    page.wait_for_timeout(1000)
    page.screenshot(path=OUT / "debug_full.png", full_page=True)
    html = page.content()
    title = page.title()
    print("Title:", title)
    print("HTML length:", len(html))
    print("Has 'GraphScript':", "GraphScript" in html)
    print("Has 'Console':", "Console" in html)
    print("Has 'NODES':", "NODES" in html)
    print("Console errors:", errors[:5] if errors else "none")
    print("All console (last 10):", all_msgs[-10:] if all_msgs else "none")
    root = page.locator("#root")
    print(" #root innerHTML length:", len(root.inner_html()) if root.count() > 0 else "no root")
    if root.count() > 0 and len(root.inner_html()) < 500:
        print(" #root content:", root.inner_html()[:500])
    # Save a snippet of body
    body_start = html.find("<body")
    if body_start >= 0:
        snippet = html[body_start:body_start+2000]
        print("\nBody snippet (first 1500 chars):")
        print(snippet[:1500])
    browser.close()
