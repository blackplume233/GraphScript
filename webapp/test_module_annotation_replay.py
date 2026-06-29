#!/usr/bin/env python3
"""E2E regression for module import/let annotation controls replaying CLI commands."""
import copy
import json
import shlex
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("Playwright not installed.")
    sys.exit(1)

ROOT = Path(__file__).parent
URL = "http://127.0.0.1:5182/"
OUT = ROOT / "test_screenshots" / "module_annotation_replay"
OUT.mkdir(parents=True, exist_ok=True)

SOURCE = "\n".join([
    "[Bind(Asset = \"existing-import\")]",
    'import "custom.d.gs";',
    "Graph ReplayHost {",
    "}",
    "[Bind(Asset = \"existing-let\")]",
    'let cached = PreviewValue("seed");',
])


def make_annotation(name, arg_name="", value=""):
    args = []
    if arg_name or value:
        args.append({"name": arg_name, "value": value})
    return {"name": name, "args": args}


def initial_state():
    return {
        "file_path": "module_annotation_replay.gs",
        "dirty": False,
        "active_graph": 0,
        "can_undo": False,
        "can_redo": False,
        "module": {
            "imports": [{
                "path": "custom.d.gs",
                "normalized_path": "custom.d.gs",
                "loaded": True,
                "is_native": False,
                "annotations": [make_annotation("Bind", "Asset", "existing-import")],
            }],
            "lets": [{
                "name": "cached",
                "type": "PreviewValue",
                "arg": "seed",
                "annotations": [make_annotation("Bind", "Asset", "existing-let")],
            }],
            "graphs": [{
                "name": "ReplayHost",
                "base_type": None,
                "annotations": [],
                "parameters": [],
                "nodes": [],
                "events": [],
                "functions": [],
            }],
        },
        "types": [],
        "schemas": [],
        "diagnostics": [],
        "command_log": [],
    }


def parse_annotation(parts, start):
    annotation = {"name": parts[start], "args": []}
    for arg in parts[start + 1:]:
        if "=" in arg:
            name, value = arg.split("=", 1)
            annotation["args"].append({"name": name, "value": value})
        else:
            annotation["args"].append({"name": "", "value": arg})
    return annotation


def upsert_annotation(annotations, annotation):
    for index, existing in enumerate(annotations):
        if existing["name"] == annotation["name"]:
            annotations[index] = annotation
            return
    annotations.append(annotation)


def erase_annotation(annotations, name):
    annotations[:] = [annotation for annotation in annotations if annotation["name"] != name]


def update_state_for_command(state, command):
    parts = shlex.split(command)
    if len(parts) < 3:
        return

    action, target = parts[0], parts[1]
    if action == "annotate" and target == "import" and len(parts) >= 4:
        path = parts[2]
        annotation = parse_annotation(parts, 3)
        for item in state["module"]["imports"]:
            if item["path"] == path or item.get("normalized_path") == path:
                upsert_annotation(item["annotations"], annotation)
                return

    if action == "annotate" and target == "let" and len(parts) >= 4:
        name = parts[2]
        annotation = parse_annotation(parts, 3)
        for item in state["module"]["lets"]:
            if item["name"] == name:
                upsert_annotation(item["annotations"], annotation)
                return

    if action == "unannotate" and target == "import" and len(parts) >= 4:
        path = parts[2]
        annotation_name = parts[3]
        for item in state["module"]["imports"]:
            if item["path"] == path or item.get("normalized_path") == path:
                erase_annotation(item["annotations"], annotation_name)
                return

    if action == "unannotate" and target == "let" and len(parts) >= 4:
        name = parts[2]
        annotation_name = parts[3]
        for item in state["module"]["lets"]:
            if item["name"] == name:
                erase_annotation(item["annotations"], annotation_name)
                return


def wait_for_server():
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(URL, timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("Vite server did not become ready")


def start_vite():
    proc = subprocess.Popen(
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5182"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        wait_for_server()
    except Exception:
        proc.terminate()
        raise
    return proc


def stop_process_tree(proc):
    if proc.poll() is not None:
        return
    if sys.platform.startswith("win"):
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def submit_annotation(card, name, arg_name="", value=""):
    card.locator('input[placeholder="Annotation"]').fill(name)
    if arg_name:
        card.locator('input[placeholder="key"]').fill(arg_name)
    if value:
        card.locator('input[placeholder="value"]').fill(value)
    card.get_by_role("button", name="Add annotation").click()


def main():
    state = initial_state()
    exec_commands = []
    proc = start_vite()
    results = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1300, "height": 820})

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
                body=SOURCE,
            ))

            def handle_exec(route):
                payload = json.loads(route.request.post_data or "{}")
                command = payload.get("command", "")
                exec_commands.append(command)
                update_state_for_command(state, command)
                state["dirty"] = True
                state["can_undo"] = True
                state["command_log"] = exec_commands[:]
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({"ok": True, "command": command, "state": copy.deepcopy(state)}),
                )

            page.route("**/api/exec", handle_exec)

            page.goto(URL, wait_until="networkidle", timeout=20000)
            page.wait_for_selector('[data-module-import="custom.d.gs"]', timeout=10000)
            page.wait_for_selector('[data-module-let="cached"]', timeout=10000)

            import_card = page.locator('[data-module-import="custom.d.gs"]')
            let_card = page.locator('[data-module-let="cached"]')

            expected = [
                'annotate import "custom.d.gs" Reviewed Asset="web-import"',
                'annotate let cached Reviewed Asset="web-let"',
                'unannotate import "custom.d.gs" Bind',
                'unannotate let cached Bind',
            ]
            submit_annotation(import_card, "Reviewed", "Asset", "web-import")
            page.wait_for_timeout(200)
            submit_annotation(let_card, "Reviewed", "Asset", "web-let")
            page.wait_for_timeout(200)
            import_card.get_by_role("button", name="Remove annotation Bind").click()
            page.wait_for_timeout(200)
            let_card.get_by_role("button", name="Remove annotation Bind").click()
            page.wait_for_timeout(200)

            results = [
                ("import annotation command replayed", expected[0] in exec_commands),
                ("let annotation command replayed", expected[1] in exec_commands),
                ("import unannotation command replayed", expected[2] in exec_commands),
                ("let unannotation command replayed", expected[3] in exec_commands),
                ("command order preserved", exec_commands[:4] == expected),
                ("command log visible", all(page.locator(f"text={command}").count() > 0 for command in expected)),
            ]
            page.screenshot(path=OUT / "module_annotation_replay.png", full_page=True)
            browser.close()
    finally:
        stop_process_tree(proc)

    failed = [name for name, ok in results if not ok]
    print("\nModule annotation replay regression")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        print("Commands:")
        for command in exec_commands:
            print(f"  {command}")
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
