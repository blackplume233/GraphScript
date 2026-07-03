#!/usr/bin/env python3
"""Regression: graph interface facts render as Entry/Return/Getters, not grouped context/input nodes.

Browser interaction is intentionally driven through the agent-browser CLI.
"""
import base64
import json
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
GS_EXE = REPO / "build" / "Release" / "gs.exe"
AGENT_BROWSER = shutil.which("agent-browser") or shutil.which("agent-browser.cmd")
SESSION = f"graphscript-interface-nodes-{int(time.time() * 1000)}"
OUT = ROOT / "test_screenshots" / "graph_interface_nodes"
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


def run_agent_browser(args, stdin=None, json_result=False):
    if not AGENT_BROWSER:
        raise RuntimeError("agent-browser CLI is not available on PATH")
    cli_args = ["--session", SESSION, *args]
    escaped = " ".join("'" + part.replace("'", "''") + "'" for part in cli_args)
    command = ["powershell", "-NoProfile", "-Command", f"& '{AGENT_BROWSER}' {escaped}"]
    log_step("agent-browser " + " ".join(args[:2]))
    proc = subprocess.Popen(
        command,
        cwd=REPO,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        stdout, stderr = proc.communicate(input=stdin, timeout=20)
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


def start_gs_serve(source_file):
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
            str(source_file),
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

    source = '''import "ue_core.d.gs";

graph InterfaceShape {
    @graph.input
    param message: FString;

    node printer {
        type PrintString;
    }
    node resetDelay {
        type Delay;
    }

    event OnStart {
        connect(context.start, printer.enter);
        bind(message, printer.message);
    }

    event OnReset {
        connect(context.start, resetDelay.enter);
        connect(resetDelay.completed, printer.enter);
        bind(message, printer.message);
    }

    function Echo {
        connect(context.start, context.done);
        bind(message, context.result);
    }
}
'''

    with tempfile.TemporaryDirectory(prefix="graphscript_interface_nodes_") as temp_dir:
        source_file = Path(temp_dir) / "interface_nodes.gs"
        source_file.write_text(source, encoding="utf-8")
        proc = start_gs_serve(source_file)
        try:
            run_agent_browser(["open", URL])
            wait_for_js("Boolean(document.querySelector('[data-blueprint-node=\"OnStart Entry\"]'))")
            event_shape = eval_js(
                """(() => ({
                  eventEntry: Boolean(document.querySelector('[data-blueprint-node="OnStart Entry"]')),
                  parameterGetter: Boolean(document.querySelector('[data-blueprint-node="message"] [data-pin-row="data-out-message"]')),
                  blockSwitcher: Boolean(document.querySelector('[data-logic-block-switcher="true"]')),
                  eventChip: Boolean(document.querySelector('[data-logic-block-chip="event:OnStart"][data-logic-block-active="true"]')),
                  resetChip: Boolean(document.querySelector('[data-logic-block-chip="event:OnReset"]')),
                  functionChip: Boolean(document.querySelector('[data-logic-block-chip="function:Echo"]')),
                  oldContext: Boolean(document.querySelector('[data-blueprint-node="context"]')),
                  oldGraphInputs: Boolean(document.querySelector('[data-blueprint-node="Graph Inputs"]')),
                  eventDonePin: Boolean(document.querySelector('[data-pin-row="exec-in-done"]')),
                  sharedPrinter: Boolean(document.querySelector('[data-blueprint-node="printer"] [data-node-shared-block-count="2"]')),
                  resetDelayVisible: Boolean(document.querySelector('[data-blueprint-node="resetDelay"]')),
                  flowLine: Boolean(document.querySelector('[data-line-id="context_exec-out-start-printer_exec-in-enter"]')),
                  dataLine: Boolean(document.querySelector('[data-line-id="message_data-out-message-printer_data-in-message"]')),
                }))()"""
            )

            eval_js(
                """(() => {
                  const chip = document.querySelector('[data-logic-block-chip="event:OnReset"]');
                  if (!chip) return false;
                  chip.click();
                  return true;
                })()"""
            )
            wait_for_js("Boolean(document.querySelector('[data-blueprint-node=\"resetDelay\"]'))")
            reset_shape = eval_js(
                """(() => ({
                  resetChipActive: Boolean(document.querySelector('[data-logic-block-chip="event:OnReset"][data-logic-block-active="true"]')),
                  printerVisible: Boolean(document.querySelector('[data-blueprint-node="printer"]')),
                  sharedPrinter: Boolean(document.querySelector('[data-blueprint-node="printer"] [data-node-shared-block-count="2"]')),
                  resetDelayVisible: Boolean(document.querySelector('[data-blueprint-node="resetDelay"]')),
                  startToPrinterLine: Boolean(document.querySelector('[data-line-id="context_exec-out-start-printer_exec-in-enter"]')),
                  resetFlowLine: Boolean(document.querySelector('[data-line-id="resetDelay_exec-out-completed-printer_exec-in-enter"]')),
                  dataLine: Boolean(document.querySelector('[data-line-id="message_data-out-message-printer_data-in-message"]')),
                }))()"""
            )

            eval_js(
                """(() => {
                  const chip = document.querySelector('[data-logic-block-chip="function:Echo"]');
                  if (!chip) return false;
                  chip.click();
                  return true;
                })()"""
            )
            wait_for_js("Boolean(document.querySelector('[data-blueprint-node=\"Return\"]'))")
            function_shape = eval_js(
                """(() => ({
                  functionEntry: Boolean(document.querySelector('[data-blueprint-node="Function Entry"]')),
                  functionReturn: Boolean(document.querySelector('[data-blueprint-node="Return"]')),
                  returnDonePin: Boolean(document.querySelector('[data-blueprint-node="Return"] [data-pin-row="exec-in-done"]')),
                  returnResultPin: Boolean(document.querySelector('[data-blueprint-node="Return"] [data-pin-row="data-in-result"]')),
                  printerVisible: Boolean(document.querySelector('[data-blueprint-node="printer"]')),
                  resetDelayVisible: Boolean(document.querySelector('[data-blueprint-node="resetDelay"]')),
                  oldContext: Boolean(document.querySelector('[data-blueprint-node="context"]')),
                  oldGraphInputs: Boolean(document.querySelector('[data-blueprint-node="Graph Inputs"]')),
                  flowLine: Boolean(document.querySelector('[data-line-id="context_exec-out-start-context_exec-in-done"]')),
                  dataLine: Boolean(document.querySelector('[data-line-id="message_data-out-message-context_data-in-result"]')),
                }))()"""
            )
            run_agent_browser(["screenshot", str(OUT / "interface_nodes.png")])
        finally:
            try:
                run_agent_browser(["close"])
            except Exception:
                pass
            stop_process_tree(proc)

    results = [
        ("event renders explicit entry node", event_shape["eventEntry"]),
        ("event renders parameter getter", event_shape["parameterGetter"]),
        ("block switcher is visible", event_shape["blockSwitcher"]),
        ("event chip is active", event_shape["eventChip"]),
        ("second event chip is available", event_shape["resetChip"]),
        ("function chip is available", event_shape["functionChip"]),
        ("event omits old context node", not event_shape["oldContext"]),
        ("event omits old graph inputs node", not event_shape["oldGraphInputs"]),
        ("event omits done input pin", not event_shape["eventDonePin"]),
        ("event marks cross-event node as shared", event_shape["sharedPrinter"]),
        ("event hides nodes only used by another event", not event_shape["resetDelayVisible"]),
        ("event context.start edge still renders", event_shape["flowLine"]),
        ("event parameter bind edge still renders", event_shape["dataLine"]),
        ("second event chip becomes active", reset_shape["resetChipActive"]),
        ("shared node appears in second event", reset_shape["printerVisible"]),
        ("shared node keeps shared marker in second event", reset_shape["sharedPrinter"]),
        ("second event renders its private node", reset_shape["resetDelayVisible"]),
        ("second event does not show first event-only edge", not reset_shape["startToPrinterLine"]),
        ("second event renders its own flow edge", reset_shape["resetFlowLine"]),
        ("second event parameter bind edge still renders", reset_shape["dataLine"]),
        ("function renders entry node", function_shape["functionEntry"]),
        ("function renders return node", function_shape["functionReturn"]),
        ("function return accepts done", function_shape["returnDonePin"]),
        ("function return accepts result", function_shape["returnResultPin"]),
        ("function hides event shared node when unreferenced", not function_shape["printerVisible"]),
        ("function hides event private node when unreferenced", not function_shape["resetDelayVisible"]),
        ("function omits old context node", not function_shape["oldContext"]),
        ("function omits old graph inputs node", not function_shape["oldGraphInputs"]),
        ("function context.done edge still renders", function_shape["flowLine"]),
        ("function context.result edge still renders", function_shape["dataLine"]),
    ]
    failed = [name for name, ok in results if not ok]
    print("\nGraph interface node rendering")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
