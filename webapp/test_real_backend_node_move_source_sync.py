#!/usr/bin/env python3
"""Regression: real canvas node moves must sync back into the Source panel."""
import time
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    raise SystemExit(1)

import test_real_backend_replay_smoke as smoke

OUT = Path(__file__).parent / "test_screenshots" / "real_backend_node_move_source_sync"
OUT.mkdir(parents=True, exist_ok=True)


def wait_for_command(prefix):
    deadline = time.time() + 8
    while time.time() < deadline:
        state = smoke.fetch_json("api/state")
        for command in reversed(state.get("command_log", [])):
            if command.startswith(prefix):
                return command, state
        time.sleep(0.1)
    raise RuntimeError(f"Command did not reach backend log: {prefix}")


def source_panel_text(page):
    text = page.locator(".view-lines").evaluate(
        "root => Array.from(root.querySelectorAll('.view-line')).map(line => line.textContent).join('\\n')"
    )
    return text.replace("\u00a0", " ")


def main():
    proc = smoke.start_gs_serve()
    results = []
    command = ""
    emitted = ""
    source_text = ""
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1500, "height": 950})
            page.goto(smoke.URL, wait_until="networkidle", timeout=20000)
            page.wait_for_selector('input[placeholder="graph"]', timeout=10000)

            page.get_by_role("textbox", name="graph", exact=True).fill("NodeMoveSourceSync")
            page.get_by_role("button", name="Create graph").click()
            wait_for_command("create_graph NodeMoveSourceSync")
            smoke.run_console_command(page, "add_node BranchOnTag mover")
            wait_for_command("add_node BranchOnTag mover")
            node_name = "mover"

            page.wait_for_selector(f'[data-blueprint-node="{node_name}"]', state="visible", timeout=10000)
            page.get_by_role("button", name="Fit View").click()
            page.wait_for_timeout(300)
            node_box = page.locator(f'[data-blueprint-node="{node_name}"]').first.bounding_box()
            if not node_box:
                raise RuntimeError(f"Node {node_name} has no visible box")
            start_x = node_box["x"] + node_box["width"] / 2
            start_y = node_box["y"] + node_box["height"] / 2
            page.mouse.move(start_x, start_y)
            page.mouse.down()
            page.mouse.move(start_x - 110, start_y + 70, steps=16)
            page.mouse.up()

            command, state = wait_for_command(f"annotate node {node_name} Position ")
            graph = smoke.graph_by_name_or_active(state, "NodeMoveSourceSync")
            emitted = smoke.fetch_text("api/emit")
            page.get_by_role("tab", name="SOURCE").click()
            page.wait_for_function(
                """() => Array.from(document.querySelectorAll('.view-line'))
                    .some(line => line.textContent.includes('@Position('))""",
                timeout=8000,
            )
            source_text = source_panel_text(page)
            page.screenshot(path=OUT / "node_move_source_synced.png", full_page=True)

            node = next(node for node in graph["nodes"] if node["instance"] == node_name)
            position = next(annotation for annotation in node["annotations"] if annotation["name"] == "Position")
            args = {arg["name"]: arg["value"] for arg in position["args"]}
            expected_line = f"@Position(X = {args['X']}, Y = {args['Y']})"
            results = [
                ("move command replayed", command.startswith(f"annotate node {node_name} Position X=")),
                ("backend state has Position", args.get("X") and args.get("Y")),
                ("emitted source has Position", expected_line in emitted),
                ("Source panel has Position", expected_line in source_text),
                ("Source panel kept import context", "import" in source_text and "mixed_declarations.d.gs" in source_text),
            ]
            browser.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nReal backend node move source sync")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Command: {command}")
    print(f"Screenshots: {OUT}")
    if failed:
        print("Emitted source:")
        print(emitted)
        print("Source panel:")
        print(source_text)
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    main()
