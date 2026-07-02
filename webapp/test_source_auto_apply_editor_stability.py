#!/usr/bin/env python3
"""Regression: background Source auto-apply must not lock Monaco.

Browser interaction is intentionally driven through the agent-browser CLI.
"""
import json
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import base64
import socket
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
GS_EXE = REPO / "build" / "Release" / "gs.exe"
def find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


PORT = find_free_port()
URL = f"http://127.0.0.1:{PORT}/"
SESSION = f"graphscript-source-auto-apply-stability-{int(time.time() * 1000)}"
AGENT_BROWSER = shutil.which("agent-browser") or shutil.which("agent-browser.cmd")
OUT = ROOT / "test_screenshots" / "source_auto_apply_editor_stability"
OUT.mkdir(parents=True, exist_ok=True)
LOG = OUT / "progress.log"


def log_step(message):
    with LOG.open("a", encoding="utf-8") as log:
        log.write(f"{time.strftime('%H:%M:%S')} {message}\n")
    print(f"  {message}", flush=True)


def run_agent_browser(args, stdin=None, json_result=False):
    if not AGENT_BROWSER:
        raise RuntimeError("agent-browser CLI is not available on PATH")
    cli_args = ["--session", SESSION, *args]
    escaped = " ".join("'" + part.replace("'", "''") + "'" for part in cli_args)
    command_text = f"& '{AGENT_BROWSER}' {escaped}"
    command = ["powershell", "-NoProfile", "-Command", command_text]
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
        stdout, stderr = proc.communicate(input=stdin, timeout=15)
    except subprocess.TimeoutExpired:
        if sys.platform.startswith("win"):
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            proc.kill()
        if args[:1] == ["open"]:
            return ""
        raise
    result = subprocess.CompletedProcess(command, proc.returncode, stdout, stderr)
    if result.returncode != 0:
        raise RuntimeError(
            "agent-browser command failed\n"
            f"command: {' '.join(command)}\n"
            f"stdout: {result.stdout}\n"
            f"stderr: {result.stderr}"
        )
    if not json_result:
        return result.stdout
    payload = json.loads(result.stdout)
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
            output = ""
            if proc.stdout:
                try:
                    output = proc.stdout.read(2000)
                except Exception:
                    output = ""
            raise RuntimeError(f"gs serve exited before ready\n{output}")
        try:
            with urllib.request.urlopen(URL, timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("gs serve did not become ready")


def wait_for_js(predicate, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if eval_js(predicate):
            return
        time.sleep(0.2)
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

graph AutoApplyStability {
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
    with tempfile.TemporaryDirectory(prefix="graphscript_source_auto_apply_") as temp_dir:
        source_file = Path(temp_dir) / "auto_apply.gs"
        source_file.write_text(source, encoding="utf-8")
        proc = start_gs_serve(source_file)
        try:
            run_agent_browser(["open", URL])
            log_step("opened page")
            wait_for_js("Boolean(window.__graphScriptSourceEditor && window.__graphScriptSourceEditor.getValue().includes('AutoApplyStability'))")
            log_step("source editor ready")
            eval_js(
                """(() => {
                  if (window.__graphScriptPatchedFetch) return true;
                  const originalFetch = window.fetch.bind(window);
                  window.__graphScriptPatchedFetch = true;
                  window.__delayDiagnostics = false;
                  window.__diagnosticsDelayHits = 0;
                  window.fetch = (input, init) => {
                    const url = typeof input === 'string' ? input : input.url;
                    if (url.includes('/api/diagnostics') && window.__delayDiagnostics) {
                      window.__diagnosticsDelayHits += 1;
                      return new Promise((resolve, reject) => {
                        window.setTimeout(() => originalFetch(input, init).then(resolve, reject), 2500);
                      });
                    }
                    return originalFetch(input, init);
                  };
                  return true;
                })()"""
            )
            log_step("fetch wrapper installed")
            eval_js("(() => { window.__delayDiagnostics = true; return true })()")
            log_step("diagnostics delay enabled")
            edited_source = source.replace("    event OnStart", "    // background edit\n    event OnStart")
            eval_js(f"window.__graphScriptSourceEditor.setValue({json.dumps(edited_source)})")
            log_step("source edited")
            wait_for_js(
                "document.querySelector('[data-source-sync-state]')?.getAttribute('data-source-sync-state') === 'checking'",
                timeout=5,
            )
            log_step("checking observed")
            read_only_during_background_check = eval_js("window.__graphScriptSourceEditor.isReadOnly()")
            wait_for_js(
                "document.querySelector('[data-source-sync-state]')?.getAttribute('data-source-sync-state') === 'synced_patch'",
                timeout=10,
            )
            log_step("synced_patch observed")
            read_only_after_apply = eval_js("window.__graphScriptSourceEditor.isReadOnly()")
            saved_source = urllib.request.urlopen(f"{URL}api/emit", timeout=5).read().decode("utf-8")
        finally:
            try:
                run_agent_browser(["close"])
            except Exception:
                pass
            stop_process_tree(proc)

    results = [
        ("background checking did not set Monaco readOnly", read_only_during_background_check is False),
        ("background apply completed source patch", "background edit" in saved_source),
        ("editor remained editable after background apply", read_only_after_apply is False),
    ]
    failed = [name for name, ok in results if not ok]
    print("\nSource background auto-apply editor stability")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
