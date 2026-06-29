#!/usr/bin/env python3
"""Smoke test stable element ids in /api/state against a real gs serve backend."""
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
GS_EXE = REPO / "build" / "Release" / "gs.exe"
DECL = REPO / "tests" / "fixtures" / "mixed_declarations.d.gs"
PORT = 8096
URL = f"http://127.0.0.1:{PORT}/"


def wait_for_server(proc):
    deadline = time.time() + 30
    while time.time() < deadline:
        if proc.poll() is not None:
            output = proc.stdout.read() if proc.stdout else ""
            raise RuntimeError(f"gs serve exited before ready\n{output}")
        try:
            with urllib.request.urlopen(URL + "api/state", timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("gs serve did not become ready")


def start_gs_serve():
    if not GS_EXE.exists():
        raise RuntimeError(f"Missing executable: {GS_EXE}")
    if not DECL.exists():
        raise RuntimeError(f"Missing declaration fixture: {DECL}")

    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    proc = subprocess.Popen(
        [str(GS_EXE), "serve", "-p", str(PORT), "-I", str(DECL)],
        cwd=REPO,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        creationflags=creationflags,
    )
    try:
        wait_for_server(proc)
    except Exception:
        proc.terminate()
        raise
    return proc


def fetch_json(path):
    with urllib.request.urlopen(URL + path, timeout=5) as res:
        return json.loads(res.read().decode("utf-8"))


def exec_command(command):
    payload = json.dumps({"command": command}).encode("utf-8")
    req = urllib.request.Request(
        URL + "api/exec",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=5) as res:
        result = json.loads(res.read().decode("utf-8"))
    if not result.get("ok"):
        raise RuntimeError(f"Command failed: {command}\n{result}")
    return result


def main():
    proc = start_gs_serve()
    results = []
    try:
        commands = [
            "create_graph StableIds",
            "param in tag GameplayTag",
            "event OnStart",
            "fn Compute",
            "add_node BranchOnTag branch",
            "add_node PlayMontage montage",
            "annotate graph Id graph-001",
            "annotate param tag Id param-tag-001",
            "annotate node branch Id node-branch-001",
            "event OnStart",
            "flow branch.matched montage.play",
            "link branch.tag tag",
            "comment branch generated note",
            "comment branch generated note",
            "meta position:branch.x 120",
            "meta position:branch.x 120",
        ]
        for command in commands:
            exec_command(command)

        state = fetch_json("api/state")
        graph = state["module"]["graphs"][0]
        event = next(block for block in graph["events"] if block["name"] == "OnStart")
        function = next(block for block in graph["functions"] if block["name"] == "Compute")
        branch = next(node for node in graph["nodes"] if node["instance"] == "branch")
        montage = next(node for node in graph["nodes"] if node["instance"] == "montage")
        param = next(param for param in graph["parameters"] if param["name"] == "tag")
        flow = event["flows"][0]
        link = event["links"][0]
        generate_comment = graph["generate"]["comments"][0]
        generate_comment_duplicate = graph["generate"]["comments"][1]
        generate_metadata = graph["generate"]["metadata"][0]
        generate_metadata_duplicate = graph["generate"]["metadata"][1]

        expected = {
            "graph": graph.get("id"),
            "param": param.get("id"),
            "branch": branch.get("id"),
            "montage": montage.get("id"),
            "event": event.get("id"),
            "function": function.get("id"),
            "flow": flow.get("id"),
            "link": link.get("id"),
            "generate_comment": generate_comment.get("id"),
            "generate_comment_duplicate": generate_comment_duplicate.get("id"),
            "generate_metadata": generate_metadata.get("id"),
            "generate_metadata_duplicate": generate_metadata_duplicate.get("id"),
        }
        results = [
            ("graph id stable", expected["graph"] == "graph:StableIds"),
            ("graph persistent id exported", graph.get("persistent_id") == "graph-001"),
            ("param id stable", expected["param"] == "param:StableIds/tag"),
            ("param persistent id exported", param.get("persistent_id") == "param-tag-001"),
            ("node id stable", expected["branch"] == "node:StableIds/branch" and expected["montage"] == "node:StableIds/montage"),
            ("node persistent id exported", branch.get("persistent_id") == "node-branch-001" and montage.get("persistent_id") == ""),
            ("event block id stable", expected["event"] == "block:StableIds/event/OnStart"),
            ("function block id stable", expected["function"] == "block:StableIds/function/Compute"),
            ("flow id stable", expected["flow"] == "flow:StableIds/event/OnStart/branch.matched->montage.play"),
            ("link id stable", expected["link"] == "link:StableIds/event/OnStart/tag->branch.tag"),
            ("generate comment id stable", expected["generate_comment"] == "generate-comment:StableIds/branch/generated note"),
            ("generate duplicate comment id disambiguated", expected["generate_comment_duplicate"] == "generate-comment:StableIds/branch/generated note#2"),
            ("generate metadata id stable", expected["generate_metadata"] == "generate-metadata:StableIds/position/branch/x/120"),
            ("generate duplicate metadata id disambiguated", expected["generate_metadata_duplicate"] == "generate-metadata:StableIds/position/branch/x/120#2"),
            ("command log remains command based", state["command_log"][-len(commands):] == commands),
        ]
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nState stable ids smoke")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
