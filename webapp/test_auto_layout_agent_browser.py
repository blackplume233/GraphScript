#!/usr/bin/env python3
"""Regression: nodes without Position annotations get a readable automatic layout.

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
SESSION = f"graphscript-auto-layout-{int(time.time() * 1000)}"
OUT = ROOT / "test_screenshots" / "auto_layout"
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

graph AutoLayout {
    @graph.input
    param message: FString;
    @graph.input
    param delaySeconds: float;

    node resetRuntimeState {
        type PrintString;
    }
    node streamingDelay {
        type Delay;
    }
    node createConvoy {
        type PrintString;
    }
    node bindSlots {
        type PrintString;
    }
    node cleanup {
        type PrintString;
    }

    event OnStart {
        connect(context.start, resetRuntimeState.enter);
        connect(resetRuntimeState.exit, streamingDelay.enter);
        connect(streamingDelay.completed, createConvoy.enter);
        connect(createConvoy.exit, bindSlots.enter);
        bind(message, resetRuntimeState.message);
        bind(delaySeconds, streamingDelay.duration);
        bind(message, createConvoy.message);
        bind(message, bindSlots.message);
    }
}
'''

    with tempfile.TemporaryDirectory(prefix="graphscript_auto_layout_") as temp_dir:
        source_file = Path(temp_dir) / "auto_layout.gs"
        source_file.write_text(source, encoding="utf-8")
        proc = start_gs_serve(source_file)
        try:
            run_agent_browser(["open", URL])
            wait_for_js("Boolean(document.querySelector('[data-blueprint-node=\"bindSlots\"]'))")
            layout = eval_js(
                """(() => {
                  const names = ['resetRuntimeState', 'streamingDelay', 'createConvoy', 'bindSlots', 'cleanup'];
                  const rects = Object.fromEntries(names.map(name => {
                    const element = document.querySelector(`[data-blueprint-node="${name}"]`);
                    if (!element) return [name, null];
                    const rect = element.getBoundingClientRect();
                    return [name, { x: rect.x, y: rect.y, width: rect.width, height: rect.height }];
                  }));
                  const overlaps = [];
                  for (let i = 0; i < names.length; i += 1) {
                    for (let j = i + 1; j < names.length; j += 1) {
                      const a = rects[names[i]];
                      const b = rects[names[j]];
                      if (!a || !b) continue;
                      const separated = a.x + a.width < b.x || b.x + b.width < a.x ||
                        a.y + a.height < b.y || b.y + b.height < a.y;
                      if (!separated) overlaps.push(`${names[i]}:${names[j]}`);
                    }
                  }
                  const getterCount = document.querySelectorAll('[data-blueprint-node-variant="parameter-getter"]').length;
                  const dataWireWidths = [...document.querySelectorAll('[data-line-id*="_data-out-"]')]
                    .map(element => element.getBBox?.().width ?? 0)
                    .filter(width => width > 0);
                  return {
                    rects,
                    overlaps,
                    getterCount,
                    maxDataWireWidth: dataWireWidths.length ? Math.max(...dataWireWidths) : 0,
                  };
                })()"""
            )
            run_agent_browser(["screenshot", str(OUT / "auto_layout.png")])
        finally:
            try:
                run_agent_browser(["close"])
            except Exception:
                pass
            stop_process_tree(proc)

    rects = layout["rects"]
    active_names = ["resetRuntimeState", "streamingDelay", "createConvoy", "bindSlots"]
    results = [
        ("active block nodes rendered", all(rects.get(name) for name in active_names)),
        ("flow chain expands left to right",
         rects["resetRuntimeState"]["x"] < rects["streamingDelay"]["x"] <
         rects["createConvoy"]["x"] < rects["bindSlots"]["x"]),
        ("flow chain stays on a readable row",
         max(abs(rects["resetRuntimeState"]["y"] - rects[name]["y"]) for name in ["streamingDelay", "createConvoy", "bindSlots"]) < 80),
        ("unused node is hidden from active block view", rects["cleanup"] is None),
        ("parameter getters render as per-bind capsules", layout["getterCount"] == 4),
        ("parameter data wires stay short", layout["maxDataWireWidth"] < 320),
        ("nodes do not overlap", len(layout["overlaps"]) == 0),
    ]
    failed = [name for name, ok in results if not ok]
    print("\nAutomatic node layout")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
