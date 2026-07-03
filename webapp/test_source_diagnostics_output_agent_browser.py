#!/usr/bin/env python3
"""Regression: Source diagnostics details live in Console and ranged errors mark Monaco.

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
SESSION = f"graphscript-source-diagnostics-output-{int(time.time() * 1000)}"
OUT = ROOT / "test_screenshots" / "source_diagnostics_output"
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


def activate_tab(title):
    run_agent_browser(["find", "text", title, "click", "--exact"])
    expected = title.upper()
    wait_for_js(
        f"""(() => [...document.querySelectorAll('.dv-tab')]
          .some(element =>
            (element.textContent || '').trim().toUpperCase() === {json.dumps(expected)} &&
            element.className.includes('dv-active-tab')
          ))()""",
        timeout=5,
    )
    return True


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

    valid_source = '''import "ue_core.d.gs";

graph DiagnosticsOutput {
    @graph.input
    param message: FString;

    node printer {
        type PrintString;
    }

    event OnStart {
        connect(context.start, printer.enter);
        bind(message, printer.message);
    }
}
'''
    invalid_source = valid_source.replace(
        "        connect(context.start, printer.enter);",
        "        connect(context.start printer.enter);",
    )

    with tempfile.TemporaryDirectory(prefix="graphscript_source_diagnostics_output_") as temp_dir:
        source_file = Path(temp_dir) / "diagnostics_output.gs"
        source_file.write_text(valid_source, encoding="utf-8")
        proc = start_gs_serve(source_file)
        try:
            run_agent_browser(["open", URL])
            wait_for_js("Boolean(window.__graphScriptSourceEditor && window.__graphScriptSourceEditor.getValue().includes('DiagnosticsOutput'))")
            log_step("source editor ready")
            eval_js("document.querySelector('[title=\"Refresh source diagnostics\"]')?.click()")
            wait_for_js("document.querySelector('[data-source-sync-state]')?.getAttribute('data-source-sync-state') !== 'checking'")

            source_omits_resolver_details = eval_js(
                """(() => Boolean(
                  document.querySelector('[data-source-editor="true"]') &&
                  !document.querySelector('[data-source-env-notice="true"]') &&
                  !document.querySelector('[data-source-resolver-metadata="true"]') &&
                  !document.body.textContent.includes('dry-run Environment')
                ))()"""
            )

            if not activate_tab("Console"):
                raise RuntimeError("Console tab not found")
            wait_for_js("Boolean(document.querySelector('[data-source-diagnostics-output=\"true\"]'))")
            console_has_resolver_details = eval_js(
                """(() => {
                  const output = document.querySelector('[data-source-diagnostics-output="true"]');
                  return Boolean(
                    output &&
                    output.querySelector('[data-source-env-notice="true"]')?.textContent.includes('dry-run') &&
                    output.querySelector('[data-source-resolver-metadata="true"]')?.textContent.includes('Resolver resolved')
                  );
                })()"""
            )

            if not activate_tab("Source"):
                raise RuntimeError("Source tab not found")
            wait_for_js("Boolean(window.__graphScriptSourceEditor)")
            eval_js(f"window.__graphScriptSourceEditor.setValue({json.dumps(invalid_source)})")
            wait_for_js(
                """(() => {
                  const markers = window.__graphScriptSourceEditor?.getDiagnosticMarkers?.() || [];
                  return markers.some(marker => marker.severity === 'error' && marker.line === 12);
                })()""",
                timeout=15,
            )
            markers = eval_js("window.__graphScriptSourceEditor.getDiagnosticMarkers()")
        finally:
            try:
                run_agent_browser(["close"])
            except Exception:
                pass
            stop_process_tree(proc)

    line_12_error = any(marker.get("severity") == "error" and marker.get("line") == 12 for marker in markers)
    results = [
        ("Source tab omits resolver details", source_omits_resolver_details),
        ("Console shows source resolver details", console_has_resolver_details),
        ("Monaco marks ranged source error on line 12", line_12_error),
    ]
    failed = [name for name, ok in results if not ok]
    print("\nSource diagnostics output placement")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
