#!/usr/bin/env python3
"""E2E regression for Blueprint-style canvas pan and context-menu separation."""
import json
import sys
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

import test_visual_edge_replay as visual

OUT = Path(__file__).parent / "test_screenshots" / "visual_canvas_pan_context"
OUT.mkdir(parents=True, exist_ok=True)


def find_blank_pane_point(page):
    point = page.evaluate("""
        () => {
            const root = document.querySelector('[data-canvas-context-root="true"]')
            const rect = root.getBoundingClientRect()
            for (let y = rect.top + 80; y < rect.bottom - 80; y += 30) {
                for (let x = rect.left + 80; x < rect.right - 80; x += 30) {
                    const el = document.elementFromPoint(x, y)
                    if (el && el.closest('[data-canvas-context-root="true"]') &&
                        el.classList.contains('react-flow__pane') &&
                        !el.closest('.react-flow__node') &&
                        !el.closest('.react-flow__minimap')) {
                        return { x, y }
                    }
                }
            }
            return null
        }
    """)
    if not point:
        raise RuntimeError("Could not find a blank canvas pane point")
    return point


def viewport_transform(page):
    return page.locator(".react-flow__viewport").get_attribute("style") or ""


def main():
    state = visual.initial_state()
    proc = visual.start_vite()
    results = []
    before_transform = ""
    after_right_drag = ""
    after_middle_drag = ""
    after_space_drag = ""
    menu_text = ""
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1400, "height": 900})

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
                body=visual.source_from_state(state),
            ))
            page.route("**/api/exec", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({"ok": True, "command": json.loads(route.request.post_data or "{}").get("command", ""), "state": state}),
            ))

            page.goto(visual.URL, wait_until="domcontentloaded", timeout=20000)
            page.wait_for_selector('[data-blueprint-node="branch"]', state="visible", timeout=10000)
            page.locator('select[title="Active event/function for visual edge edits"]').select_option("event:BeginPlay")
            page.wait_for_timeout(500)
            before_transform = viewport_transform(page)

            point = find_blank_pane_point(page)
            page.mouse.move(point["x"], point["y"])
            page.mouse.down(button="right")
            page.mouse.move(point["x"] + 130, point["y"] + 90, steps=18)
            page.mouse.up(button="right")
            page.wait_for_timeout(500)
            after_right_drag = viewport_transform(page)
            right_drag_menu_visible = page.locator('[data-canvas-context-menu="true"]').count() > 0

            point = find_blank_pane_point(page)
            page.mouse.move(point["x"], point["y"])
            page.mouse.down(button="middle")
            page.mouse.move(point["x"] - 90, point["y"] + 55, steps=16)
            page.mouse.up(button="middle")
            page.wait_for_timeout(500)
            after_middle_drag = viewport_transform(page)

            point = find_blank_pane_point(page)
            page.keyboard.down("Space")
            page.mouse.move(point["x"], point["y"])
            page.mouse.down(button="left")
            page.mouse.move(point["x"] + 75, point["y"] - 80, steps=16)
            page.mouse.up(button="left")
            page.keyboard.up("Space")
            page.wait_for_timeout(500)
            after_space_drag = viewport_transform(page)

            point = find_blank_pane_point(page)
            page.mouse.click(point["x"], point["y"], button="right")
            menu = page.locator('[data-canvas-context-menu="true"]')
            menu.wait_for(state="visible", timeout=3000)
            menu_text = menu.inner_text(timeout=3000)
            menu_placeholder = menu.locator("input").get_attribute("placeholder") or ""
            page.screenshot(path=OUT / "01_pan_and_context_menu.png", full_page=True)

            results = [
                ("right drag panned viewport", after_right_drag != before_transform),
                ("right drag did not open context menu", not right_drag_menu_visible),
                ("middle drag panned viewport", after_middle_drag != after_right_drag),
                ("space left-drag panned viewport", after_space_drag != after_middle_drag),
                ("right click opens add-node menu", "ADD NODE AT" in menu_text and "Search node type" in menu_placeholder),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nVisual canvas pan/context regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Before: {before_transform}")
    print(f"After right drag: {after_right_drag}")
    print(f"After middle drag: {after_middle_drag}")
    print(f"After space drag: {after_space_drag}")
    print(f"Menu: {menu_text[:120]}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
