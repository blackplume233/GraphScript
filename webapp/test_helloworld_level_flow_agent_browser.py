#!/usr/bin/env python3
"""Regression: the HelloWorld fixture opens as a full level-flow graph.

Browser interaction is intentionally driven through the agent-browser CLI.
"""
import base64
import json
import shutil
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
GS_EXE = REPO / "build" / "Release" / "gs.exe"
SOURCE = REPO / "tests" / "fixtures" / "minimal.gs"
AGENT_BROWSER = shutil.which("agent-browser") or shutil.which("agent-browser.cmd")
SESSION = f"graphscript-helloworld-flow-{int(time.time() * 1000)}"
OUT = ROOT / "test_screenshots" / "helloworld_level_flow"
OUT.mkdir(parents=True, exist_ok=True)
LOG = OUT / "progress.log"


def find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


PORT = find_free_port()
URL = f"http://127.0.0.1:{PORT}/"


def log_step(message):
    with LOG.open("a", encoding="utf-8") as log:
        log.write(f"{time.strftime('%H:%M:%S')} {message}\n")
    print(f"  {message}", flush=True)


def run_agent_browser(args, json_result=False):
    if not AGENT_BROWSER:
        raise RuntimeError("agent-browser CLI is not available on PATH")
    cli_args = ["--session", SESSION, *args]
    escaped = " ".join("'" + part.replace("'", "''") + "'" for part in cli_args)
    command = ["powershell", "-NoProfile", "-Command", f"& '{AGENT_BROWSER}' {escaped}"]
    log_step("agent-browser " + " ".join(args[:2]))
    proc = subprocess.Popen(
        command,
        cwd=REPO,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        stdout, stderr = proc.communicate(timeout=25)
    except subprocess.TimeoutExpired:
        if sys.platform.startswith("win"):
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            proc.kill()
        if args[:1] == ["open"]:
            return ""
        raise
    if proc.returncode != 0:
        raise RuntimeError(
            "agent-browser command failed\n"
            f"command: {' '.join(command)}\n"
            f"stdout: {stdout}\n"
            f"stderr: {stderr}"
        )
    if not json_result:
        return stdout
    payload = json.loads(stdout)
    if payload.get("error"):
        raise RuntimeError(f"agent-browser eval error: {payload['error']}")
    return payload["data"]["result"]


def eval_js(script):
    encoded = base64.b64encode(script.encode("utf-8")).decode("ascii")
    return run_agent_browser(["eval", "-b", encoded, "--json"], json_result=True)


def wait_for_server(proc):
    deadline = time.time() + 30
    while time.time() < deadline:
        if proc.poll() is not None:
            output = proc.stdout.read(2000) if proc.stdout else ""
            raise RuntimeError(f"gs serve exited before ready\n{output}")
        try:
            with urllib.request.urlopen(URL, timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("gs serve did not become ready")


def wait_for_js(predicate, timeout=12):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if eval_js(predicate):
            return
        time.sleep(0.25)
    raise RuntimeError(f"Timed out waiting for JS predicate: {predicate}")


def start_gs_serve():
    log_step(f"starting gs serve on {PORT}")
    proc = subprocess.Popen(
        [
            str(GS_EXE),
            "serve",
            "-p",
            str(PORT),
            "-I",
            str(REPO / "presets" / "ue_core.d.gs"),
            "-i",
            str(SOURCE),
        ],
        cwd=REPO,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    wait_for_server(proc)
    log_step("gs serve ready")
    return proc


def stop_process_tree(proc):
    if proc.poll() is not None:
        return
    if sys.platform.startswith("win"):
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    else:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    LOG.write_text("", encoding="utf-8")
    if not GS_EXE.exists():
        raise SystemExit(f"Missing {GS_EXE}; build gs before running this test")
    if not SOURCE.exists():
        raise SystemExit(f"Missing {SOURCE}")

    proc = start_gs_serve()
    try:
        run_agent_browser(["open", URL])
        wait_for_js("Boolean(document.querySelector('[data-blueprint-node=\"OnStart Entry\"]'))")
        initial = eval_js(
            """(async () => {
              const state = await fetch('/api/state').then(response => response.json());
              const graph = state.module.graphs[0];
              return {
                nodes: graph.nodes.length,
                events: graph.events.length,
                functions: graph.functions.length,
                diagnostics: state.diagnostics?.length ?? 0,
                chips: document.querySelectorAll('[data-logic-block-chip]').length,
                onStartEntry: Boolean(document.querySelector('[data-blueprint-node="OnStart Entry"]')),
                oldContext: Boolean(document.querySelector('[data-blueprint-node="context"]')),
                oldGraphInputs: Boolean(document.querySelector('[data-blueprint-node="Graph Inputs"]')),
                getters: document.querySelectorAll('[data-blueprint-node-variant="parameter-getter"]').length,
                activeNodes: document.querySelectorAll('[data-blueprint-node]').length,
                inactiveAlertNodeVisible: Boolean(document.querySelector('[data-blueprint-node="alertRaised"]')),
              };
            })()"""
        )

        eval_js(
            """(() => {
              const chip = document.querySelector('[data-logic-block-chip="event:OnAllEntitiesCreated"]');
              if (!chip) return false;
              chip.click();
              return true;
            })()"""
        )
        wait_for_js("Boolean(document.querySelector('[data-logic-block-chip=\"event:OnAllEntitiesCreated\"][data-logic-block-active=\"true\"]'))")
        wait_for_js("Boolean(document.querySelector('[data-blueprint-node=\"configureTeamFormation\"]'))", timeout=25)
        all_created = eval_js(
            """(() => {
              const shared = document.querySelector('[data-blueprint-node="setFirstPatrolTarget"] [data-node-shared-block-count]');
              return {
                active: Boolean(document.querySelector('[data-logic-block-chip="event:OnAllEntitiesCreated"][data-logic-block-active="true"]')),
                startupNode: Boolean(document.querySelector('[data-blueprint-node="configureTeamFormation"]')),
                sharedFirstTarget: shared ? Number(shared.getAttribute('data-node-shared-block-count')) : 0,
                unrelatedAlertNodeVisible: Boolean(document.querySelector('[data-blueprint-node="alertRaised"]')),
              };
            })()"""
        )

        eval_js(
            """(() => {
              const chip = document.querySelector('[data-logic-block-chip="event:OnPatrolAlert"]');
              if (!chip) return false;
              chip.click();
              return true;
            })()"""
        )
        wait_for_js("Boolean(document.querySelector('[data-logic-block-chip=\"event:OnPatrolAlert\"][data-logic-block-active=\"true\"]'))")
        wait_for_js("Boolean(document.querySelector('[data-blueprint-node=\"regroupGate\"]'))", timeout=25)
        alert = eval_js(
            """(() => {
              const shared = document.querySelector('[data-blueprint-node="setNextPatrolTarget"] [data-node-shared-block-count]');
              return {
                active: Boolean(document.querySelector('[data-logic-block-chip="event:OnPatrolAlert"][data-logic-block-active="true"]')),
                alertNode: Boolean(document.querySelector('[data-blueprint-node="alertRaised"]')),
                regroupDelay: Boolean(document.querySelector('[data-blueprint-node="regroupGate"]')),
                sharedNextTarget: shared ? Number(shared.getAttribute('data-node-shared-block-count')) : 0,
                unrelatedStartupNodeVisible: Boolean(document.querySelector('[data-blueprint-node="configureTeamFormation"]')),
              };
            })()"""
        )

        eval_js(
            """(() => {
              const chip = document.querySelector('[data-logic-block-chip="function:GetAlertStatus"]');
              if (!chip) return false;
              chip.click();
              return true;
            })()"""
        )
        wait_for_js("Boolean(document.querySelector('[data-logic-block-chip=\"function:GetAlertStatus\"][data-logic-block-active=\"true\"]'))")
        wait_for_js("Boolean(document.querySelector('[data-blueprint-node=\"Return\"]'))", timeout=25)
        function_view = eval_js(
            """(() => ({
              active: Boolean(document.querySelector('[data-logic-block-chip="function:GetAlertStatus"][data-logic-block-active="true"]')),
              entry: Boolean(document.querySelector('[data-blueprint-node="Function Entry"]')),
              returnNode: Boolean(document.querySelector('[data-blueprint-node="Return"]')),
              alertNodeVisible: Boolean(document.querySelector('[data-blueprint-node="alertRaised"]')),
              resultLine: Boolean(document.querySelector('[data-line-id="alertMessage_data-out-alertMessage-context_data-in-result"]')),
            }))()"""
        )
        run_agent_browser(["screenshot", str(OUT / "helloworld_level_flow.png")])
    finally:
        try:
            run_agent_browser(["close"])
        except Exception:
            pass
        stop_process_tree(proc)

    results = [
        ("fixture has full node count", initial["nodes"] == 62),
        ("fixture has eleven events", initial["events"] == 11),
        ("fixture has three functions", initial["functions"] == 3),
        ("fixture has no diagnostics", initial["diagnostics"] == 0),
        ("all logic block chips render", initial["chips"] == 14),
        ("OnStart renders entry node", initial["onStartEntry"]),
        ("old context node is absent", not initial["oldContext"]),
        ("old Graph Inputs node is absent", not initial["oldGraphInputs"]),
        ("OnStart renders parameter getter capsules", initial["getters"] >= 10),
        ("OnStart hides unrelated alert node", not initial["inactiveAlertNodeVisible"]),
        ("AllEntitiesCreated chip activates", all_created["active"]),
        ("AllEntitiesCreated renders startup chain", all_created["startupNode"]),
        ("shared first target is marked", all_created["sharedFirstTarget"] >= 3),
        ("AllEntitiesCreated hides alert node", not all_created["unrelatedAlertNodeVisible"]),
        ("PatrolAlert chip activates", alert["active"]),
        ("PatrolAlert renders alert chain", alert["alertNode"] and alert["regroupDelay"]),
        ("shared next target is marked", alert["sharedNextTarget"] >= 2),
        ("PatrolAlert hides startup-only node", not alert["unrelatedStartupNodeVisible"]),
        ("GetAlertStatus chip activates", function_view["active"]),
        ("function view renders entry and return", function_view["entry"] and function_view["returnNode"]),
        ("function view hides event nodes", not function_view["alertNodeVisible"]),
        ("function result bind renders", function_view["resultLine"]),
    ]
    failed = [name for name, ok in results if not ok]
    print("\nHelloWorld level-flow graph")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
