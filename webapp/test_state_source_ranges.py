#!/usr/bin/env python3
"""Smoke test element source_range in /api/state against a real gs serve backend."""
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
GS_EXE = REPO / "build" / "Release" / "gs.exe"
CORE_DECL = REPO / "presets" / "ue_core.d.gs"
PORT = 8097
URL = f"http://127.0.0.1:{PORT}/"
OUT = ROOT / "test_screenshots" / "state_source_ranges"
SOURCE = OUT / "state_source_ranges.gs"


def write_source():
    OUT.mkdir(parents=True, exist_ok=True)
    SOURCE.write_text(
        "\n".join([
            'import "ue_core.d.gs";',
            'let spawn_point = SoftObjectPath("spawn");',
            "",
            '[Comment("title", "Span graph")]',
            "Graph SpanGraph : TraceGraph {",
            '    in msg : FString = SoftObjectPath("Hello");',
            "    [Position(X = 10, Y = 20)]",
            '    PrintString logger{message = msg, asset = SoftObjectPath("Asset")};',
            "    Delay wait{};",
            "    event OnStart {",
            "        logger.exit(wait.enter);",
            "        link logger.message = msg;",
            "    }",
            "    function Compute {",
            "    }",
            "    generate {",
            '        [Id("gen-comment-001")]',
            '        Comment logger = "Legacy note";',
            '        [PersistentId("gen-meta-001")]',
            '        position:logger.x(SoftObjectPath("Generated"));',
            "    }",
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
    if not CORE_DECL.exists():
        raise RuntimeError(f"Missing declaration fixture: {CORE_DECL}")
    write_source()

    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    proc = subprocess.Popen(
        [str(GS_EXE), "serve", "-p", str(PORT), "-I", str(CORE_DECL), "-i", str(SOURCE)],
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
        import_def = state["module"]["imports"][0]
        let_def = state["module"]["lets"][0]
        graph = state["module"]["graphs"][0]
        graph_annotation = graph["annotations"][0]
        param = graph["parameters"][0]
        logger = next(node for node in graph["nodes"] if node["instance"] == "logger")
        logger_annotation = logger["annotations"][0]
        logger_initializer_fields = logger.get("initializer_fields", [])
        logger_asset_field = next(field for field in logger_initializer_fields if field["name"] == "asset")
        event = graph["events"][0]
        function = graph["functions"][0]
        flow = event["flows"][0]
        link = event["links"][0]
        generate = graph["generate"]
        generate_comment = generate["comments"][0]
        generate_comment_annotation = generate_comment["annotations"][0]
        generate_metadata = generate["metadata"][0]
        generate_metadata_annotation = generate_metadata["annotations"][0]

        results = [
            ("import id stable", import_def.get("id") == "import:ue_core.d.gs"),
            ("import source range", start_of(import_def) == {"line": 1, "column": 1}),
            ("import path source range", start_of({"source_range": import_def.get("path_source_range", {})}) == {"line": 1, "column": 8}),
            ("let id stable", let_def.get("id") == "let:spawn_point"),
            ("let source range", start_of(let_def) == {"line": 2, "column": 1}),
            ("let name source range", start_of({"source_range": let_def.get("name_source_range", {})}) == {"line": 2, "column": 5}),
            ("let type source range", start_of({"source_range": let_def.get("type_source_range", {})}) == {"line": 2, "column": 19}),
            ("let constructor source range", start_of({"source_range": let_def.get("constructor_source_range", {})}) == {"line": 2, "column": 19}),
            ("let arg source range", start_of({"source_range": let_def.get("arg_source_range", {})}) == {"line": 2, "column": 34}),
            ("graph annotation source range", start_of(graph_annotation) == {"line": 4, "column": 2}),
            ("graph annotation arg source range", start_of(graph_annotation["args"][0]) == {"line": 4, "column": 10}),
            ("graph source range", start_of(graph) == {"line": 5, "column": 1}),
            ("graph name source range", start_of({"source_range": graph.get("name_source_range", {})}) == {"line": 5, "column": 7}),
            ("graph base type source range", start_of({"source_range": graph.get("base_type_source_range", {})}) == {"line": 5, "column": 19}),
            ("param source range", start_of(param) == {"line": 6, "column": 5}),
            ("param name source range", start_of({"source_range": param.get("name_source_range", {})}) == {"line": 6, "column": 8}),
            ("param type source range", start_of({"source_range": param.get("type_source_range", {})}) == {"line": 6, "column": 14}),
            ("param default source range", start_of({"source_range": param.get("default_source_range", {})}) == {"line": 6, "column": 24}),
            ("param default constructor source range", start_of({"source_range": param.get("default_constructor_source_range", {})}) == {"line": 6, "column": 24}),
            ("param default constructor type source range", start_of({"source_range": param.get("default_constructor_type_source_range", {})}) == {"line": 6, "column": 24}),
            ("param default constructor arg source range", start_of({"source_range": param.get("default_constructor_arg_source_range", {})}) == {"line": 6, "column": 39}),
            ("node annotation source range", start_of(logger_annotation) == {"line": 7, "column": 6}),
            ("node annotation arg source range", start_of(logger_annotation["args"][0]) == {"line": 7, "column": 15}),
            ("node source range", start_of(logger) == {"line": 8, "column": 5}),
            ("node type source range", start_of({"source_range": logger.get("type_source_range", {})}) == {"line": 8, "column": 5}),
            ("node instance source range", start_of({"source_range": logger.get("instance_source_range", {})}) == {"line": 8, "column": 17}),
            ("node init source range", start_of({"source_range": logger.get("init_source_range", {})}) == {"line": 8, "column": 24}),
            ("node initializer field value constructor source range", start_of({"source_range": logger_asset_field.get("value_constructor_source_range", {})}) == {"line": 8, "column": 47}),
            ("node initializer field value constructor type source range", start_of({"source_range": logger_asset_field.get("value_constructor_type_source_range", {})}) == {"line": 8, "column": 47}),
            ("node initializer field value constructor arg source range", start_of({"source_range": logger_asset_field.get("value_constructor_arg_source_range", {})}) == {"line": 8, "column": 62}),
            ("event source range", start_of(event) == {"line": 10, "column": 5}),
            ("event name source range", start_of({"source_range": event.get("name_source_range", {})}) == {"line": 10, "column": 11}),
            ("flow source range", start_of(flow) == {"line": 11, "column": 9}),
            ("flow from endpoint source range", start_of({"source_range": flow.get("from_endpoint_source_range", {})}) == {"line": 11, "column": 9}),
            ("flow to endpoint source range", start_of({"source_range": flow.get("to_endpoint_source_range", {})}) == {"line": 11, "column": 21}),
            ("flow from node source range", start_of({"source_range": flow.get("from_node_source_range", {})}) == {"line": 11, "column": 9}),
            ("flow from pin source range", start_of({"source_range": flow.get("from_pin_source_range", {})}) == {"line": 11, "column": 16}),
            ("flow to node source range", start_of({"source_range": flow.get("to_node_source_range", {})}) == {"line": 11, "column": 21}),
            ("flow to pin source range", start_of({"source_range": flow.get("to_pin_source_range", {})}) == {"line": 11, "column": 26}),
            ("link source range", start_of(link) == {"line": 12, "column": 9}),
            ("link target endpoint source range", start_of({"source_range": link.get("target_endpoint_source_range", {})}) == {"line": 12, "column": 14}),
            ("link source endpoint source range", start_of({"source_range": link.get("source_endpoint_source_range", {})}) == {"line": 12, "column": 31}),
            ("link target node source range", start_of({"source_range": link.get("target_node_source_range", {})}) == {"line": 12, "column": 14}),
            ("link target pin source range", start_of({"source_range": link.get("target_pin_source_range", {})}) == {"line": 12, "column": 21}),
            ("link source node source range", start_of({"source_range": link.get("source_node_source_range", {})}) == {"line": 12, "column": 31}),
            ("function source range", start_of(function) == {"line": 14, "column": 5}),
            ("function name source range", start_of({"source_range": function.get("name_source_range", {})}) == {"line": 14, "column": 14}),
            ("generate source range", start_of(generate) == {"line": 16, "column": 5}),
            ("generate comment id stable", generate_comment.get("id") == "generate-comment:SpanGraph/logger/Legacy note"),
            ("generate comment persistent id", generate_comment.get("persistent_id") == "gen-comment-001"),
            ("generate comment annotation source range", start_of(generate_comment_annotation) == {"line": 17, "column": 10}),
            ("generate comment source range", start_of(generate_comment) == {"line": 18, "column": 9}),
            ("generate comment instance source range", start_of({"source_range": generate_comment.get("instance_source_range", {})}) == {"line": 18, "column": 17}),
            ("generate comment text source range", start_of({"source_range": generate_comment.get("text_source_range", {})}) == {"line": 18, "column": 26}),
            ("generate metadata id stable", generate_metadata.get("id") == 'generate-metadata:SpanGraph/position/logger/x/SoftObjectPath("Generated")'),
            ("generate metadata persistent id", generate_metadata.get("persistent_id") == "gen-meta-001"),
            ("generate metadata annotation source range", start_of(generate_metadata_annotation) == {"line": 19, "column": 10}),
            ("generate metadata source range", start_of(generate_metadata) == {"line": 20, "column": 9}),
            ("generate metadata scope source range", start_of({"source_range": generate_metadata.get("scope_source_range", {})}) == {"line": 20, "column": 9}),
            ("generate metadata node source range", start_of({"source_range": generate_metadata.get("node_source_range", {})}) == {"line": 20, "column": 18}),
            ("generate metadata property source range", start_of({"source_range": generate_metadata.get("property_source_range", {})}) == {"line": 20, "column": 25}),
            ("generate metadata value source range", start_of({"source_range": generate_metadata.get("value_source_range", {})}) == {"line": 20, "column": 27}),
            ("generate metadata constructor source range", start_of({"source_range": generate_metadata.get("value_constructor_source_range", {})}) == {"line": 20, "column": 27}),
            ("generate metadata constructor type source range", start_of({"source_range": generate_metadata.get("value_constructor_type_source_range", {})}) == {"line": 20, "column": 27}),
            ("generate metadata constructor arg source range", start_of({"source_range": generate_metadata.get("value_constructor_arg_source_range", {})}) == {"line": 20, "column": 42}),
            ("source range coexists with stable id", graph.get("id") == "graph:SpanGraph" and flow.get("id") == "flow:SpanGraph/event/OnStart/logger.exit->wait.enter"),
        ]
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    failed = [name for name, ok in results if not ok]
    print("\nState source ranges smoke")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Source: {SOURCE}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
