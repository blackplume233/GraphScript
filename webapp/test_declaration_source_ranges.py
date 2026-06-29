#!/usr/bin/env python3
"""Smoke test declaration source_range in /api/state against a real gs serve backend."""
import json
import subprocess
import sys
import time
import urllib.request
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
GS_EXE = REPO / "build" / "Release" / "gs.exe"
PORT = 8099
URL = f"http://127.0.0.1:{PORT}/"
OUT = ROOT / "test_screenshots" / "declaration_source_ranges"
DECL = OUT / "declaration_source_ranges.d.gs"


def write_declaration():
    OUT.mkdir(parents=True, exist_ok=True)
    DECL.write_text(
        "\n".join([
            "declare type DeclSmokeValue : constructible;",
            "declare Node DeclSmokeNode {",
            "    exec in enter;",
            "    data in value : DeclSmokeValue;",
            "    exec out done;",
            "}",
            "declare Schema DeclSmokeSchema {",
            "    max_exec_fan_out: 1;",
            "    allow_exec_fan_in: true;",
            "    strict_type_match: false;",
            "    default_payload: DeclSmokeValue(\"schema\");",
            "}",
            "",
        ]),
        encoding="utf-8",
    )


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
    write_declaration()

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


def start_of(item):
    return item.get("source_range", {}).get("start", {})


def main():
    proc = start_gs_serve()
    results = []
    try:
        state = fetch_json("api/state")
        declaration_source = fetch_json("api/declaration_source?path=" + urllib.parse.quote(str(DECL)))
        declared_type = next(item for item in state["declared_types"] if item["name"] == "DeclSmokeValue")
        node = next(item for item in state["types"] if item["type_name"] == "DeclSmokeNode")
        enter_pin = next(pin for pin in node["pins"] if pin["name"] == "enter")
        value_pin = next(pin for pin in node["pins"] if pin["name"] == "value")
        schema = next(item for item in state["schemas"] if item["name"] == "DeclSmokeSchema")
        max_fan_out = next(field for field in schema["fields"] if field["name"] == "max_exec_fan_out")
        default_payload = next(field for field in schema["fields"] if field["name"] == "default_payload")

        results = [
            ("declared type source range", start_of(declared_type) == {"line": 1, "column": 1}),
            ("declared type source file", declared_type.get("source_file") == str(DECL)),
            ("declared type name source range", start_of({"source_range": declared_type.get("name_source_range", {})}) == {"line": 1, "column": 14}),
            ("declared type constructible preserved", declared_type.get("constructible") is True),
            ("node declaration source range", start_of(node) == {"line": 2, "column": 1}),
            ("node declaration source file", node.get("source_file") == str(DECL)),
            ("node declaration name source range", start_of({"source_range": node.get("name_source_range", {})}) == {"line": 2, "column": 14}),
            ("exec pin declaration source range", start_of(enter_pin) == {"line": 3, "column": 5}),
            ("exec pin declaration source file", enter_pin.get("source_file") == str(DECL)),
            ("exec pin name source range", start_of({"source_range": enter_pin.get("name_source_range", {})}) == {"line": 3, "column": 13}),
            ("data pin declaration source range", start_of(value_pin) == {"line": 4, "column": 5}),
            ("data pin name source range", start_of({"source_range": value_pin.get("name_source_range", {})}) == {"line": 4, "column": 13}),
            ("data pin type source range", start_of({"source_range": value_pin.get("type_source_range", {})}) == {"line": 4, "column": 21}),
            ("schema declaration source range", start_of(schema) == {"line": 7, "column": 1}),
            ("schema declaration source file", schema.get("source_file") == str(DECL)),
            ("schema declaration name source range", start_of({"source_range": schema.get("name_source_range", {})}) == {"line": 7, "column": 16}),
            ("schema field source range", start_of(max_fan_out) == {"line": 8, "column": 5}),
            ("schema field source file", max_fan_out.get("source_file") == str(DECL)),
            ("schema field name source range", start_of({"source_range": max_fan_out.get("name_source_range", {})}) == {"line": 8, "column": 5}),
            ("schema field value source range", start_of({"source_range": max_fan_out.get("value_source_range", {})}) == {"line": 8, "column": 23}),
            ("schema field value preserved", max_fan_out.get("value") == "1"),
            ("schema field constructor preserved", default_payload.get("value") == 'DeclSmokeValue("schema")'),
            ("schema field constructor source range", start_of({"source_range": default_payload.get("value_constructor_source_range", {})}) == {"line": 11, "column": 22}),
            ("schema field constructor type source range", start_of({"source_range": default_payload.get("value_constructor_type_source_range", {})}) == {"line": 11, "column": 22}),
            ("schema field constructor arg source range", start_of({"source_range": default_payload.get("value_constructor_arg_source_range", {})}) == {"line": 11, "column": 37}),
            ("pin type preserved", value_pin.get("type") == "DeclSmokeValue"),
            ("declaration source endpoint ok", declaration_source.get("ok") is True),
            ("declaration source endpoint content", "declare Node DeclSmokeNode" in declaration_source.get("source", "")),
            ("declaration source endpoint hash", len(declaration_source.get("content_hash", "")) == 16),
        ]
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nDeclaration source ranges smoke")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Declaration: {DECL}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
