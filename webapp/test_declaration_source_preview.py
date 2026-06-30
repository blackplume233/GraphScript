#!/usr/bin/env python3
"""E2E smoke for opening resolved .d.gs declaration source in the source preview."""
import json
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
URL = "http://127.0.0.1:5181/"
OUT = ROOT / "test_screenshots" / "declaration_source_preview"
OUT.mkdir(parents=True, exist_ok=True)

SOURCE = "\n".join([
    'import "custom.d.gs";',
    "Graph PreviewHost : PreviewSchema {",
    '    in payload : PreviewValue = PreviewValue("payload");',
    '    CustomPreview worker{payload = PreviewValue("node")}; CustomPreview rawWorker{PreviewValue("raw")};',
    "    event OnRun {",
    "        flow worker.enter worker.exit;",
    "        link worker.payload = payload; link worker.payload = worker.payload;",
    "    }",
    "}",
    'let cached = PreviewValue("seed");',
])

DECLARATION_SOURCE = "\n".join([
    "declare type PreviewValue;",
    "declare Node CustomPreview {",
    "    exec in enter;",
    "    exec out exit;",
    "    data in payload : PreviewValue;",
    "}",
    "declare Schema PreviewSchema {",
    "    max_exec_fan_out = 1;",
    "    allow_exec_fan_in = true;",
    "    default_payload = PreviewValue(\"schema\");",
    "}",
])


def bind_annotation(value):
    return {
        "name": "Bind",
        "source_range": {
            "start": {"line": 1, "column": 1},
            "end": {"line": 1, "column": 36},
        },
        "name_source_range": {
            "start": {"line": 1, "column": 2},
            "end": {"line": 1, "column": 6},
        },
        "args": [{
            "name": "Asset",
            "value": value,
            "source_range": {
                "start": {"line": 1, "column": 7},
                "end": {"line": 1, "column": 35},
            },
            "name_source_range": {
                "start": {"line": 1, "column": 7},
                "end": {"line": 1, "column": 12},
            },
            "value_source_range": {
                "start": {"line": 1, "column": 15},
                "end": {"line": 1, "column": 35},
            },
            "value_constructor_source_range": {
                "start": {"line": 1, "column": 14},
                "end": {"line": 1, "column": 26},
            },
            "value_constructor_type_source_range": {
                "start": {"line": 1, "column": 14},
                "end": {"line": 1, "column": 26},
            },
            "value_constructor_arg_source_range": {
                "start": {"line": 1, "column": 28},
                "end": {"line": 1, "column": 34},
            },
        }],
    }


def empty_state():
    return {
        "file_path": "preview_host.gs",
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
                "source_range": {
                    "start": {"line": 1, "column": 1},
                    "end": {"line": 1, "column": 22},
                },
                "path_source_range": {
                    "start": {"line": 1, "column": 8},
                    "end": {"line": 1, "column": 21},
                },
                "annotations": [bind_annotation('PreviewValue("import")')],
            }],
            "lets": [{
                "name": "cached",
                "type": "PreviewValue",
                "arg": "seed",
                "source_range": {
                    "start": {"line": 9, "column": 1},
                    "end": {"line": 9, "column": 35},
                },
                "name_source_range": {
                    "start": {"line": 9, "column": 5},
                    "end": {"line": 9, "column": 11},
                },
                "type_source_range": {
                    "start": {"line": 9, "column": 14},
                    "end": {"line": 9, "column": 26},
                },
                "constructor_source_range": {
                    "start": {"line": 9, "column": 14},
                    "end": {"line": 9, "column": 34},
                },
                "arg_source_range": {
                    "start": {"line": 9, "column": 27},
                    "end": {"line": 9, "column": 33},
                },
                "annotations": [bind_annotation('PreviewValue("let")')],
            }],
            "graphs": [{
                "name": "PreviewHost",
                "base_type": "PreviewSchema",
                "source_range": {
                    "start": {"line": 2, "column": 1},
                    "end": {"line": 8, "column": 2},
                },
                "name_source_range": {
                    "start": {"line": 2, "column": 7},
                    "end": {"line": 2, "column": 18},
                },
                "base_type_source_range": {
                    "start": {"line": 2, "column": 21},
                    "end": {"line": 2, "column": 34},
                },
                "annotations": [],
                "parameters": [{
                    "name": "payload",
                    "type": "PreviewValue",
                    "direction": "in",
                    "default": 'PreviewValue("payload")',
                    "source_range": {
                        "start": {"line": 3, "column": 5},
                        "end": {"line": 3, "column": 57},
                    },
                    "name_source_range": {
                        "start": {"line": 3, "column": 8},
                        "end": {"line": 3, "column": 15},
                    },
                    "type_source_range": {
                        "start": {"line": 3, "column": 18},
                        "end": {"line": 3, "column": 30},
                    },
                    "default_source_range": {
                        "start": {"line": 3, "column": 33},
                        "end": {"line": 3, "column": 56},
                    },
                    "default_constructor_source_range": {
                        "start": {"line": 3, "column": 33},
                        "end": {"line": 3, "column": 56},
                    },
                    "default_constructor_type_source_range": {
                        "start": {"line": 3, "column": 33},
                        "end": {"line": 3, "column": 45},
                    },
                    "default_constructor_arg_source_range": {
                        "start": {"line": 3, "column": 46},
                        "end": {"line": 3, "column": 55},
                    },
                    "annotations": [{
                        "name": "Bind",
                        "source_range": {
                            "start": {"line": 4, "column": 5},
                            "end": {"line": 4, "column": 25},
                        },
                        "name_source_range": {
                            "start": {"line": 4, "column": 6},
                            "end": {"line": 4, "column": 9},
                        },
                        "args": [{
                            "name": "Asset",
                            "value": 'PreviewValue("anno")',
                            "source_range": {
                                "start": {"line": 4, "column": 11},
                                "end": {"line": 4, "column": 24},
                            },
                            "name_source_range": {
                                "start": {"line": 4, "column": 11},
                                "end": {"line": 4, "column": 16},
                            },
                            "value_source_range": {
                                "start": {"line": 4, "column": 19},
                                "end": {"line": 4, "column": 24},
                            },
                            "value_constructor_source_range": {
                                "start": {"line": 4, "column": 19},
                                "end": {"line": 4, "column": 38},
                            },
                            "value_constructor_type_source_range": {
                                "start": {"line": 4, "column": 19},
                                "end": {"line": 4, "column": 31},
                            },
                            "value_constructor_arg_source_range": {
                                "start": {"line": 4, "column": 32},
                                "end": {"line": 4, "column": 37},
                            },
                        }],
                    }],
                }],
                "nodes": [{
                    "type": "CustomPreview",
                    "instance": "worker",
                    "init": 'payload = PreviewValue("node")',
                    "source_range": {
                        "start": {"line": 4, "column": 5},
                        "end": {"line": 4, "column": 58},
                    },
                    "type_source_range": {
                        "start": {"line": 4, "column": 5},
                        "end": {"line": 4, "column": 18},
                    },
                    "instance_source_range": {
                        "start": {"line": 4, "column": 19},
                        "end": {"line": 4, "column": 25},
                    },
                    "init_source_range": {
                        "start": {"line": 4, "column": 25},
                        "end": {"line": 4, "column": 56},
                    },
                    "initializer_fields": [{
                        "name": "payload",
                        "value": 'PreviewValue("node")',
                        "source_range": {
                            "start": {"line": 4, "column": 26},
                            "end": {"line": 4, "column": 55},
                        },
                        "name_source_range": {
                            "start": {"line": 4, "column": 26},
                            "end": {"line": 4, "column": 33},
                        },
                        "value_source_range": {
                            "start": {"line": 4, "column": 36},
                            "end": {"line": 4, "column": 55},
                        },
                        "value_constructor_source_range": {
                            "start": {"line": 4, "column": 36},
                            "end": {"line": 4, "column": 55},
                        },
                        "value_constructor_type_source_range": {
                            "start": {"line": 4, "column": 36},
                            "end": {"line": 4, "column": 48},
                        },
                        "value_constructor_arg_source_range": {
                            "start": {"line": 4, "column": 49},
                            "end": {"line": 4, "column": 54},
                        },
                    }],
                    "annotations": [],
                }, {
                    "type": "CustomPreview",
                    "instance": "rawWorker",
                    "init": 'PreviewValue("raw")',
                    "source_range": {
                        "start": {"line": 4, "column": 59},
                        "end": {"line": 4, "column": 104},
                    },
                    "type_source_range": {
                        "start": {"line": 4, "column": 59},
                        "end": {"line": 4, "column": 72},
                    },
                    "instance_source_range": {
                        "start": {"line": 4, "column": 73},
                        "end": {"line": 4, "column": 82},
                    },
                    "init_source_range": {
                        "start": {"line": 4, "column": 83},
                        "end": {"line": 4, "column": 102},
                    },
                    "init_constructor_source_range": {
                        "start": {"line": 4, "column": 83},
                        "end": {"line": 4, "column": 102},
                    },
                    "init_constructor_type_source_range": {
                        "start": {"line": 4, "column": 83},
                        "end": {"line": 4, "column": 95},
                    },
                    "init_constructor_arg_source_range": {
                        "start": {"line": 4, "column": 96},
                        "end": {"line": 4, "column": 101},
                    },
                    "initializer_fields": [],
                    "annotations": [],
                }],
                "events": [{
                    "name": "OnRun",
                    "kind": "event",
                    "source_range": {
                        "start": {"line": 5, "column": 5},
                        "end": {"line": 8, "column": 6},
                    },
                    "name_source_range": {
                        "start": {"line": 5, "column": 11},
                        "end": {"line": 5, "column": 16},
                    },
                    "annotations": [{
                        "name": "Bind",
                        "source_range": {
                            "start": {"line": 5, "column": 5},
                            "end": {"line": 5, "column": 39},
                        },
                        "name_source_range": {
                            "start": {"line": 5, "column": 6},
                            "end": {"line": 5, "column": 10},
                        },
                        "args": [{
                            "name": "Asset",
                            "value": 'PreviewValue("event")',
                            "source_range": {
                                "start": {"line": 5, "column": 11},
                                "end": {"line": 5, "column": 38},
                            },
                            "name_source_range": {
                                "start": {"line": 5, "column": 11},
                                "end": {"line": 5, "column": 16},
                            },
                            "value_source_range": {
                                "start": {"line": 5, "column": 19},
                                "end": {"line": 5, "column": 38},
                            },
                            "value_constructor_source_range": {
                                "start": {"line": 5, "column": 19},
                                "end": {"line": 5, "column": 39},
                            },
                            "value_constructor_type_source_range": {
                                "start": {"line": 5, "column": 19},
                                "end": {"line": 5, "column": 31},
                            },
                            "value_constructor_arg_source_range": {
                                "start": {"line": 5, "column": 32},
                                "end": {"line": 5, "column": 38},
                            },
                        }],
                    }],
                    "flows": [{
                        "from_node": "worker",
                        "from_pin": "enter",
                        "to_node": "worker",
                        "to_pin": "exit",
                        "source_range": {
                            "start": {"line": 6, "column": 9},
                            "end": {"line": 6, "column": 39},
                        },
                        "from_endpoint_source_range": {
                            "start": {"line": 6, "column": 14},
                            "end": {"line": 6, "column": 26},
                        },
                        "from_node_source_range": {
                            "start": {"line": 6, "column": 14},
                            "end": {"line": 6, "column": 20},
                        },
                        "from_pin_source_range": {
                            "start": {"line": 6, "column": 21},
                            "end": {"line": 6, "column": 26},
                        },
                        "to_endpoint_source_range": {
                            "start": {"line": 6, "column": 27},
                            "end": {"line": 6, "column": 38},
                        },
                        "to_node_source_range": {
                            "start": {"line": 6, "column": 27},
                            "end": {"line": 6, "column": 33},
                        },
                        "to_pin_source_range": {
                            "start": {"line": 6, "column": 34},
                            "end": {"line": 6, "column": 38},
                        },
                        "annotations": [{
                            "name": "Bind",
                            "source_range": {
                                "start": {"line": 6, "column": 9},
                                "end": {"line": 6, "column": 43},
                            },
                            "name_source_range": {
                                "start": {"line": 6, "column": 10},
                                "end": {"line": 6, "column": 14},
                            },
                            "args": [{
                                "name": "Asset",
                                "value": 'PreviewValue("flow")',
                                "source_range": {
                                    "start": {"line": 6, "column": 15},
                                    "end": {"line": 6, "column": 42},
                                },
                                "name_source_range": {
                                    "start": {"line": 6, "column": 15},
                                    "end": {"line": 6, "column": 20},
                                },
                                "value_source_range": {
                                    "start": {"line": 6, "column": 23},
                                    "end": {"line": 6, "column": 42},
                                },
                                "value_constructor_source_range": {
                                    "start": {"line": 6, "column": 23},
                                    "end": {"line": 6, "column": 42},
                                },
                                "value_constructor_type_source_range": {
                                    "start": {"line": 6, "column": 23},
                                    "end": {"line": 6, "column": 35},
                                },
                                "value_constructor_arg_source_range": {
                                    "start": {"line": 6, "column": 36},
                                    "end": {"line": 6, "column": 41},
                                },
                            }],
                        }],
                    }],
                    "links": [
                        {
                            "target_node": "worker",
                            "target_pin": "payload",
                            "source_node": "payload",
                            "source_pin": "",
                            "source_range": {
                                "start": {"line": 7, "column": 9},
                                "end": {"line": 7, "column": 39},
                            },
                            "target_endpoint_source_range": {
                                "start": {"line": 7, "column": 14},
                                "end": {"line": 7, "column": 28},
                            },
                            "target_node_source_range": {
                                "start": {"line": 7, "column": 14},
                                "end": {"line": 7, "column": 20},
                            },
                            "target_pin_source_range": {
                                "start": {"line": 7, "column": 21},
                                "end": {"line": 7, "column": 28},
                            },
                            "source_endpoint_source_range": {
                                "start": {"line": 7, "column": 31},
                                "end": {"line": 7, "column": 38},
                            },
                            "source_node_source_range": {
                                "start": {"line": 7, "column": 31},
                                "end": {"line": 7, "column": 38},
                            },
                            "annotations": [{
                                "name": "Bind",
                                "source_range": {
                                    "start": {"line": 7, "column": 9},
                                    "end": {"line": 7, "column": 43},
                                },
                                "name_source_range": {
                                    "start": {"line": 7, "column": 10},
                                    "end": {"line": 7, "column": 14},
                                },
                                "args": [{
                                    "name": "Asset",
                                    "value": 'PreviewValue("link")',
                                    "source_range": {
                                        "start": {"line": 7, "column": 15},
                                        "end": {"line": 7, "column": 42},
                                    },
                                    "name_source_range": {
                                        "start": {"line": 7, "column": 15},
                                        "end": {"line": 7, "column": 20},
                                    },
                                    "value_source_range": {
                                        "start": {"line": 7, "column": 23},
                                        "end": {"line": 7, "column": 42},
                                    },
                                    "value_constructor_source_range": {
                                        "start": {"line": 7, "column": 23},
                                        "end": {"line": 7, "column": 42},
                                    },
                                    "value_constructor_type_source_range": {
                                        "start": {"line": 7, "column": 23},
                                        "end": {"line": 7, "column": 35},
                                    },
                                    "value_constructor_arg_source_range": {
                                        "start": {"line": 7, "column": 36},
                                        "end": {"line": 7, "column": 41},
                                    },
                                }],
                            }],
                        },
                        {
                            "target_node": "worker",
                            "target_pin": "payload",
                            "source_node": "worker",
                            "source_pin": "payload",
                            "source_range": {
                                "start": {"line": 7, "column": 40},
                                "end": {"line": 7, "column": 77},
                            },
                            "target_endpoint_source_range": {
                                "start": {"line": 7, "column": 45},
                                "end": {"line": 7, "column": 59},
                            },
                            "target_node_source_range": {
                                "start": {"line": 7, "column": 45},
                                "end": {"line": 7, "column": 51},
                            },
                            "target_pin_source_range": {
                                "start": {"line": 7, "column": 52},
                                "end": {"line": 7, "column": 59},
                            },
                            "source_endpoint_source_range": {
                                "start": {"line": 7, "column": 62},
                                "end": {"line": 7, "column": 76},
                            },
                            "source_node_source_range": {
                                "start": {"line": 7, "column": 62},
                                "end": {"line": 7, "column": 68},
                            },
                            "source_pin_source_range": {
                                "start": {"line": 7, "column": 69},
                                "end": {"line": 7, "column": 76},
                            },
                            "annotations": [],
                        },
                    ],
                }],
                "functions": [],
                "generate": {
                    "source_range": {
                        "start": {"line": 9, "column": 5},
                        "end": {"line": 11, "column": 6},
                    },
                    "comments": [],
                    "metadata": [{
                        "id": 'generate-metadata:PreviewHost/position/worker/payload/PreviewValue("generated")',
                        "source_range": {
                            "start": {"line": 10, "column": 9},
                            "end": {"line": 10, "column": 63},
                        },
                        "scope_source_range": {
                            "start": {"line": 10, "column": 9},
                            "end": {"line": 10, "column": 17},
                        },
                        "node_source_range": {
                            "start": {"line": 10, "column": 18},
                            "end": {"line": 10, "column": 24},
                        },
                        "property_source_range": {
                            "start": {"line": 10, "column": 25},
                            "end": {"line": 10, "column": 32},
                        },
                        "value_source_range": {
                            "start": {"line": 10, "column": 33},
                            "end": {"line": 10, "column": 62},
                        },
                        "value_constructor_source_range": {
                            "start": {"line": 10, "column": 33},
                            "end": {"line": 10, "column": 62},
                        },
                        "value_constructor_type_source_range": {
                            "start": {"line": 10, "column": 33},
                            "end": {"line": 10, "column": 45},
                        },
                        "value_constructor_arg_source_range": {
                            "start": {"line": 10, "column": 46},
                            "end": {"line": 10, "column": 57},
                        },
                        "scope": "position",
                        "node": "worker",
                        "property": "payload",
                        "value": 'PreviewValue("generated")',
                        "annotations": [{
                            "name": "Bind",
                            "source_range": {
                                "start": {"line": 10, "column": 64},
                                "end": {"line": 10, "column": 90},
                            },
                            "name_source_range": {
                                "start": {"line": 10, "column": 65},
                                "end": {"line": 10, "column": 69},
                            },
                            "args": [{
                                "name": "Asset",
                                "value": 'PreviewValue("meta")',
                                "source_range": {
                                    "start": {"line": 10, "column": 70},
                                    "end": {"line": 10, "column": 89},
                                },
                                "name_source_range": {
                                    "start": {"line": 10, "column": 70},
                                    "end": {"line": 10, "column": 75},
                                },
                                "value_source_range": {
                                    "start": {"line": 10, "column": 78},
                                    "end": {"line": 10, "column": 89},
                                },
                                "value_constructor_source_range": {
                                    "start": {"line": 10, "column": 78},
                                    "end": {"line": 10, "column": 98},
                                },
                                "value_constructor_type_source_range": {
                                    "start": {"line": 10, "column": 78},
                                    "end": {"line": 10, "column": 90},
                                },
                                "value_constructor_arg_source_range": {
                                    "start": {"line": 10, "column": 91},
                                    "end": {"line": 10, "column": 97},
                                },
                            }],
                        }],
                    }],
                },
            }],
        },
        "declared_types": [{
            "name": "PreviewValue",
            "source_file": "custom.d.gs",
            "source_range": {
                "start": {"line": 1, "column": 1},
                "end": {"line": 1, "column": 27},
            },
            "name_source_range": {
                "start": {"line": 1, "column": 14},
                "end": {"line": 1, "column": 26},
            },
            "constructible": True,
            "annotations": [bind_annotation('PreviewValue("decl")')],
        }],
        "types": [{
            "type_name": "CustomPreview",
            "source_file": "custom.d.gs",
            "source_range": {
                "start": {"line": 2, "column": 1},
                "end": {"line": 6, "column": 2},
            },
            "name_source_range": {
                "start": {"line": 2, "column": 14},
                "end": {"line": 2, "column": 27},
            },
            "is_native": True,
            "source_graph": "",
            "tags": [],
            "annotations": [bind_annotation('PreviewValue("declnode")')],
            "pins": [
                {
                    "name": "enter",
                    "source_file": "custom.d.gs",
                    "source_range": {
                        "start": {"line": 3, "column": 5},
                        "end": {"line": 3, "column": 19},
                    },
                    "name_source_range": {
                        "start": {"line": 3, "column": 13},
                        "end": {"line": 3, "column": 18},
                    },
                    "kind": "exec",
                    "direction": "in",
                    "type": "",
                    "annotations": [],
                },
                {
                    "name": "exit",
                    "source_file": "custom.d.gs",
                    "source_range": {
                        "start": {"line": 4, "column": 5},
                        "end": {"line": 4, "column": 18},
                    },
                    "name_source_range": {
                        "start": {"line": 4, "column": 14},
                        "end": {"line": 4, "column": 18},
                    },
                    "kind": "exec",
                    "direction": "out",
                    "type": "",
                    "annotations": [],
                },
                {
                    "name": "payload",
                    "source_file": "custom.d.gs",
                    "source_range": {
                        "start": {"line": 5, "column": 5},
                        "end": {"line": 5, "column": 36},
                    },
                    "name_source_range": {
                        "start": {"line": 5, "column": 13},
                        "end": {"line": 5, "column": 20},
                    },
                    "type_source_range": {
                        "start": {"line": 5, "column": 23},
                        "end": {"line": 5, "column": 35},
                    },
                    "kind": "data",
                    "direction": "in",
                    "type": "PreviewValue",
                    "annotations": [bind_annotation('PreviewValue("declpin")')],
                },
            ],
        }],
        "schemas": [{
            "name": "PreviewSchema",
            "source_file": "custom.d.gs",
            "source_range": {
                "start": {"line": 7, "column": 1},
                "end": {"line": 11, "column": 2},
            },
            "name_source_range": {
                "start": {"line": 7, "column": 16},
                "end": {"line": 7, "column": 29},
            },
            "max_exec_fan_out": 1,
            "allow_exec_fan_in": True,
            "strict_type_match": False,
            "annotations": [bind_annotation('PreviewValue("declschema")')],
            "fields": [
                {
                    "name": "max_exec_fan_out",
                    "value": "1",
                    "source_file": "custom.d.gs",
                    "source_range": {
                        "start": {"line": 8, "column": 5},
                        "end": {"line": 8, "column": 26},
                    },
                    "name_source_range": {
                        "start": {"line": 8, "column": 5},
                        "end": {"line": 8, "column": 21},
                    },
                    "value_source_range": {
                        "start": {"line": 8, "column": 24},
                        "end": {"line": 8, "column": 25},
                    },
                    "annotations": [],
                },
                {
                    "name": "allow_exec_fan_in",
                    "value": "true",
                    "source_file": "custom.d.gs",
                    "source_range": {
                        "start": {"line": 9, "column": 5},
                        "end": {"line": 9, "column": 30},
                    },
                    "name_source_range": {
                        "start": {"line": 9, "column": 5},
                        "end": {"line": 9, "column": 22},
                    },
                    "value_source_range": {
                        "start": {"line": 9, "column": 25},
                        "end": {"line": 9, "column": 29},
                    },
                    "annotations": [],
                },
                {
                    "name": "default_payload",
                    "value": 'PreviewValue("schema")',
                    "source_file": "custom.d.gs",
                    "source_range": {
                        "start": {"line": 10, "column": 5},
                        "end": {"line": 10, "column": 45},
                    },
                    "name_source_range": {
                        "start": {"line": 10, "column": 5},
                        "end": {"line": 10, "column": 19},
                    },
                    "value_source_range": {
                        "start": {"line": 10, "column": 23},
                        "end": {"line": 10, "column": 45},
                    },
                    "value_constructor_source_range": {
                        "start": {"line": 10, "column": 23},
                        "end": {"line": 10, "column": 45},
                    },
                    "value_constructor_type_source_range": {
                        "start": {"line": 10, "column": 23},
                        "end": {"line": 10, "column": 35},
                    },
                    "value_constructor_arg_source_range": {
                        "start": {"line": 10, "column": 36},
                        "end": {"line": 10, "column": 44},
                    },
                    "annotations": [bind_annotation('PreviewValue("declfield")')],
                },
            ],
        }],
        "diagnostics": [],
        "command_log": [],
    }


RESOLVED_ENVIRONMENT = {
    "mode": "resolved",
    "declarations": [{
        "path": "custom.d.gs",
        "normalized_path": "custom.d.gs",
        "content_hash": "declpreview0001",
        "status": "loaded",
        "message": "Loaded declaration into dry-run environment",
        "command": "import custom.d.gs",
        "parent_path": "",
        "parent_normalized_path": "",
        "import_chain": "source -> custom.d.gs",
        "depth": 1,
    }],
    "environment_hash": "declaration-preview-env",
    "type_count": 1,
    "node_type_count": 1,
    "schema_count": 0,
    "limits": {
        "max_imports": 32,
        "max_import_depth": 8,
        "max_file_bytes": 1048576,
        "max_total_bytes": 4194304,
    },
}


def wait_for_server(proc):
    deadline = time.time() + 30
    while time.time() < deadline:
        if proc.poll() is not None:
            output = proc.stdout.read() if proc.stdout else ""
            raise RuntimeError(f"Vite server exited before ready\n{output}")
        try:
            with urllib.request.urlopen(URL, timeout=1) as res:
                if res.status == 200:
                    return
        except Exception:
            time.sleep(0.25)
    raise RuntimeError("Vite server did not become ready")


def stop_process_tree(proc):
    if proc.poll() is not None:
        return
    subprocess.run(
        ["taskkill", "/PID", str(proc.pid), "/T", "/F"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def start_vite():
    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    proc = subprocess.Popen(
        ["npm.cmd", "run", "dev", "--", "--host", "127.0.0.1", "--port", "5181", "--strictPort"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        creationflags=creationflags,
    )
    try:
        wait_for_server(proc)
    except Exception:
        stop_process_tree(proc)
        raise
    return proc


def main():
    proc = start_vite()
    results = []
    declaration_source_text = DECLARATION_SOURCE
    declaration_source_hash = "declpreview0001"
    exec_commands = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1300, "height": 820})

            def handle_exec(route):
                nonlocal declaration_source_text, declaration_source_hash
                payload = json.loads(route.request.post_data or "{}")
                command = payload.get("command", "")
                exec_commands.append(command)
                ok = True
                error = ""
                if command == "apply_import_type_rename custom.d.gs PreviewValue RenamedValue declpreview0001":
                    declaration_source_text = declaration_source_text.replace("PreviewValue", "RenamedValue")
                    declaration_source_hash = "declpreview0002"
                elif command == "apply_import_schema_field_rename custom.d.gs PreviewSchema default_payload fallback_payload declpreview0002":
                    declaration_source_text = declaration_source_text.replace(
                        "default_payload = RenamedValue(\"schema\");",
                        "fallback_payload = RenamedValue(\"schema\");",
                    )
                    declaration_source_hash = "declpreview0003"
                elif command == "apply_import_schema_rename custom.d.gs PreviewSchema RenamedSchema declpreview0003":
                    declaration_source_text = declaration_source_text.replace("PreviewSchema", "RenamedSchema")
                    declaration_source_hash = "declpreview0004"
                elif command == "apply_import_node_pin_rename custom.d.gs CustomPreview enter begin declpreview0004":
                    declaration_source_text = declaration_source_text.replace("exec in enter;", "exec in begin;")
                    declaration_source_hash = "declpreview0005"
                elif command == "apply_import_node_rename custom.d.gs CustomPreview RenamedPreview declpreview0005":
                    declaration_source_text = declaration_source_text.replace("CustomPreview", "RenamedPreview")
                    declaration_source_hash = "declpreview0006"
                elif (
                    command.startswith("apply_import_node_rename") or
                    command.startswith("apply_import_node_pin_rename") or
                    command.startswith("apply_import_type_rename") or
                    command.startswith("apply_import_schema_rename") or
                    command.startswith("apply_import_schema_field_rename")
                ):
                    ok = False
                    error = "Unexpected declaration rename command"

                next_state = empty_state()
                next_state["command_log"] = exec_commands[:]
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({
                        "ok": ok,
                        "command": command,
                        "output": "ok" if ok else "",
                        "error": error,
                        "state": next_state,
                    }),
                )

            def handle_declaration_source(route):
                route.fulfill(
                    status=200,
                    content_type="application/json",
                    body=json.dumps({
                        "ok": True,
                        "path": "custom.d.gs",
                        "normalized_path": "custom.d.gs",
                        "content_hash": declaration_source_hash,
                        "source": declaration_source_text,
                    }),
                )

            def wait_for_command(command, timeout_ms=5000):
                deadline = time.time() + timeout_ms / 1000
                while time.time() < deadline:
                    if command in exec_commands:
                        return True
                    page.wait_for_timeout(50)
                return command in exec_commands

            page.route("**/api/state", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps(empty_state()),
            ))
            page.route("**/api/emit", lambda route: route.fulfill(
                status=200,
                content_type="text/plain",
                body=SOURCE,
            ))
            page.route("**/api/diagnostics", lambda route: route.fulfill(
                status=200,
                content_type="application/json",
                body=json.dumps({
                    "ok": True,
                    "stage": "asset",
                    "diagnostics": [],
                    "environment": RESOLVED_ENVIRONMENT,
                }),
            ))
            page.route("**/api/exec", handle_exec)
            page.route("**/api/declaration_source?**", handle_declaration_source)

            page.goto(URL)
            page.wait_for_selector('[data-source-sync-state="empty"]', timeout=10000)
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-import-tree="true"]', timeout=5000)
            page.locator('[data-source-declaration-open="custom.d.gs"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            declaration_label_visible = page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs"
            declaration_content_visible = page.locator("text=declare Node CustomPreview").count() == 1
            declaration_edit_disabled = page.get_by_title("Edit source").is_disabled()
            session_source_hidden = page.locator('[data-source-line]', has_text="Graph PreviewHost").count() == 0

            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            session_label_visible = page.locator('[data-source-preview-label="true"]').inner_text() == "Session"
            session_source_visible = page.locator('[data-source-line]', has_text="Graph PreviewHost").count() == 1
            session_edit_enabled = page.get_by_title("Edit source").is_enabled()

            search_box = page.get_by_label("Search graph")
            search_box.fill("CustomPreview")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview"]',
                timeout=5000,
            )
            declaration_search_node_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview"]'
                ).count() == 1
            )
            declaration_search_node_detail = (
                "declare Node" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            declaration_search_node_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "CustomPreview"
            )

            search_box.fill("CustomPreview.enter")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview.enter"]',
                timeout=5000,
            )
            declaration_search_pin_visible = (
                page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview.enter"]'
                ).count() == 1
            )
            declaration_search_pin_detail = (
                "exec" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview.enter"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview.enter"]'
            ).click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            declaration_search_pin_highlighted = page.locator('[data-source-range="active"]').inner_text() == "enter"

            page.locator('[data-source-jump="import-declaration-0"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            import_declaration_source_opened = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-line]', has_text="declare type PreviewValue").count() == 1
            )
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-import-0-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            import_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)

            page.locator('[data-source-jump="graph-base-type-def-PreviewHost"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            graph_base_schema_definition_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewSchema"
            )
            page.locator('[data-source-jump="param-type-def-payload"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            parameter_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="param-default-constructor-type-def-payload"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            parameter_default_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-param-payload-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="generate-metadata-0-constructor-type-def"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            generate_metadata_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="generate-metadata-0-property-def"]').click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            generate_metadata_property_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "payload"
            page.locator('[data-source-jump="generate-metadata-0-property-type-def"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            generate_metadata_property_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-generate-metadata-0-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            generate_metadata_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-event-OnRun-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            event_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-flow-event-OnRun-worker-enter-worker-exit-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            flow_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-link-event-OnRun-payload--worker-payload-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            link_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="let-type-def-cached"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            let_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="let-constructor-type-def-cached"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            let_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-let-cached-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            let_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="decl-type-name-PreviewValue"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_type_name_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-decl-type-PreviewValue-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_type_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.locator('[data-source-jump="annotation-arg-constructor-type-decl-type-PreviewValue-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_type_annotation_constructor_type_source_opened = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )
            page.locator('[data-source-jump="decl-node-type-name-CustomPreview"]').click()
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            declaration_node_type_name_highlighted = page.locator('[data-source-range="active"]').inner_text() == "CustomPreview"
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-decl-node-type-CustomPreview-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_node_type_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.locator('[data-source-jump="annotation-arg-constructor-type-decl-node-type-CustomPreview-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_node_type_annotation_constructor_type_source_opened = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )
            page.locator('[data-source-jump="decl-node-pin-name-CustomPreview-enter"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            declaration_node_pin_name_highlighted = page.locator('[data-source-range="active"]').inner_text() == "enter"
            page.locator('[data-source-jump="decl-node-pin-type-CustomPreview-payload"]').click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_node_pin_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="decl-node-pin-type-def-CustomPreview-payload"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_node_pin_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.locator('[data-source-jump="annotation-arg-constructor-type-decl-node-pin-CustomPreview-payload-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_node_pin_annotation_constructor_type_source_opened = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )
            page.locator('[data-source-jump="decl-schema-name-PreviewSchema"]').click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            declaration_schema_name_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewSchema"
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-decl-schema-PreviewSchema-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_schema_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.locator('[data-source-jump="annotation-arg-constructor-type-decl-schema-PreviewSchema-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_schema_annotation_constructor_type_source_opened = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )
            page.locator('[data-source-jump="decl-schema-field-value-PreviewSchema-max_exec_fan_out"]').click()
            page.wait_for_selector('[data-source-line="8"][data-source-focused="true"]', timeout=5000)
            declaration_schema_field_value_highlighted = page.locator('[data-source-range="active"]').inner_text() == "1"
            page.locator('[data-source-jump="decl-schema-field-constructor-type-PreviewSchema-default_payload"]').click()
            page.wait_for_selector('[data-source-line="10"][data-source-focused="true"]', timeout=5000)
            declaration_schema_field_constructor_type_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="decl-schema-field-constructor-type-def-PreviewSchema-default_payload"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_schema_field_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="annotation-arg-constructor-type-def-decl-schema-field-PreviewSchema-default_payload-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_schema_field_annotation_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.locator('[data-source-jump="annotation-arg-constructor-type-decl-schema-field-PreviewSchema-default_payload-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_schema_field_annotation_constructor_type_source_opened = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )
            page.locator('[data-source-jump="flow-from-pin-def-event-OnRun-worker-enter"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            flow_source_pin_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "enter"
            page.locator('[data-source-jump="flow-to-pin-def-event-OnRun-worker-exit"]').click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            flow_target_pin_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "exit"
            page.locator('[data-source-jump="link-event-OnRun-payload--worker-payload-source-param-type-def"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            link_source_parameter_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="link-event-OnRun-payload--worker-payload-target-pin-def"]').click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            link_target_pin_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "payload"
            page.locator('[data-source-jump="link-event-OnRun-payload--worker-payload-target-pin-type-def"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            link_target_pin_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="link-event-OnRun-worker-payload-worker-payload-source-pin-def"]').click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            link_source_pin_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "payload"
            page.locator('[data-source-jump="link-event-OnRun-worker-payload-worker-payload-source-pin-type-def"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            link_source_pin_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"

            page.locator(".node-card", has_text="worker").first.click()
            page.wait_for_selector('[data-node-instance="worker"]', timeout=5000)
            page.locator('[data-source-jump="node-type-def-worker"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            node_type_definition_from_declaration = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "CustomPreview"
            )
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.locator('[data-source-jump="annotation-arg-constructor-type-node-type-worker-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            selected_node_type_annotation_constructor_type_source_opened = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )
            page.locator('[data-source-jump="node-pin-name-worker-enter"]').click()
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            node_pin_name_from_declaration = page.locator('[data-source-range="active"]').inner_text() == "enter"
            page.locator('[data-source-jump="node-pin-type-def-worker-payload"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            node_pin_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.locator('[data-source-jump="annotation-arg-constructor-type-node-pin-worker-payload-Bind-Asset"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            selected_node_pin_annotation_constructor_type_source_opened = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )
            page.locator('[data-source-jump="init-field-name-def-node-worker-payload"]').click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            initializer_field_name_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "payload"
            page.locator('[data-source-jump="init-field-name-type-def-node-worker-payload"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            initializer_field_name_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator('[data-source-jump="init-field-constructor-type-def-node-worker-payload"]').click()
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            initializer_field_constructor_type_definition_highlighted = page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            page.locator(".node-card", has_text="rawWorker").click()
            page.wait_for_selector('[data-node-instance="rawWorker"]', timeout=5000)
            page.locator('[data-source-jump="node-init-constructor-type-def-rawWorker"]').click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            raw_initializer_constructor_type_definition_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)

            search_box.fill("PreviewHost")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="graph"][data-graph-search-result-label="PreviewHost"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="graph"][data-graph-search-result-label="PreviewHost"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.wait_for_selector('[data-source-line="2"][data-source-focused="true"]', timeout=5000)
            graph_search_source_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "Session" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewHost"
            )

            search_box.fill("rawWorker")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="rawWorker"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="node"][data-graph-search-result-label="rawWorker"]'
            ).click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            node_search_source_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "Session" and
                page.locator('[data-source-range="active"]').inner_text() == "rawWorker"
            )

            search_box.fill("event OnRun")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="block"][data-graph-search-result-label="event OnRun"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="block"][data-graph-search-result-label="event OnRun"]'
            ).click()
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            block_search_source_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "Session" and
                page.locator('[data-source-range="active"]').inner_text() == "OnRun"
            )

            page.locator('[data-logic-flow-source="worker.enter -> worker.exit"] [data-action="delete-flow"]').click()
            inline_flow_delete_command_recorded = (
                wait_for_command("event OnRun") and
                wait_for_command("unflow worker.enter worker.exit")
            )
            page.locator('[data-logic-link-source="payload -> worker.payload"] [data-action="delete-link"]').click()
            inline_link_delete_command_recorded = (
                wait_for_command("event OnRun") and
                wait_for_command("unlink worker.payload")
            )
            page.get_by_title("Refresh source preview").click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)

            search_box.fill("param payload default PreviewValue")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="param payload default PreviewValue"]',
                timeout=5000,
            )
            source_reference_param_detail = (
                "constructor type reference" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="param payload default PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="param payload default PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            source_reference_param_constructor_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "Session" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("3:33-3:45")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="param payload default PreviewValue"]',
                timeout=5000,
            )
            source_reference_range_param_detail = (
                "range 3:33-3:45" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="param payload default PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="param payload default PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.wait_for_selector('[data-source-line="3"][data-source-focused="true"]', timeout=5000)
            source_reference_range_param_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "Session" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("node rawWorker init PreviewValue")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="node rawWorker init PreviewValue"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="node rawWorker init PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-line="4"][data-source-focused="true"]', timeout=5000)
            source_reference_raw_initializer_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "Session" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("flow event OnRun worker.enter")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="flow event OnRun worker.enter"]',
                timeout=5000,
            )
            source_reference_flow_detail = (
                "flow source endpoint" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="flow event OnRun worker.enter"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="flow event OnRun worker.enter"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="session"]', timeout=5000)
            page.wait_for_selector('[data-source-line="6"][data-source-focused="true"]', timeout=5000)
            source_reference_flow_endpoint_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "Session" and
                page.locator('[data-source-range="active"]').inner_text() == "worker.enter"
            )

            search_box.fill("link event OnRun payload source")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="link event OnRun payload source"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="link event OnRun payload source"]'
            ).click()
            page.wait_for_selector('[data-source-line="7"][data-source-focused="true"]', timeout=5000)
            source_reference_link_endpoint_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "Session" and
                page.locator('[data-source-range="active"]').inner_text() == "payload"
            )

            search_box.fill("declare pin CustomPreview.payload type PreviewValue")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_pin_detail = (
                "declaration pin type reference" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_pin_type_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("custom.d.gs:5:23-5:35")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_file_range_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_file_range_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("custom.d.gs:5:23")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_file_point_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_file_point_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("custom.d.gs(5,23)")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_paren_point_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_paren_point_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("custom.d.gs(5,23): error GS9999: preview failure")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_log_line_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_log_line_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill(r"C:\logs\custom.d.gs(5,23): error GS9999: preview failure")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_path_log_line_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_path_log_line_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill(r'"C:\build logs\custom.d.gs"(5,23): error GS9999: preview failure')
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_quoted_path_log_line_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_quoted_path_log_line_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill(r"'C:\build logs\custom.d.gs'(5,23): error GS9999: preview failure")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_single_quoted_path_log_line_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_single_quoted_path_log_line_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("custom.d.gs:5")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_file_line_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_file_line_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("custom.d.gs#L5")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_hash_line_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_hash_line_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("custom.d.gs#L5-L5")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]',
                timeout=5000,
            )
            declaration_source_reference_hash_line_range_detail = (
                "at custom.d.gs:5:23-5:35" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare pin CustomPreview.payload type PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="5"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_hash_line_range_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("declare schema PreviewSchema.default_payload value PreviewValue")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare schema PreviewSchema.default_payload value PreviewValue"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare schema PreviewSchema.default_payload value PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="10"][data-source-focused="true"]', timeout=5000)
            declaration_source_reference_schema_field_constructor_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("declare annotation type PreviewValue Bind.Asset PreviewValue")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare annotation type PreviewValue Bind.Asset PreviewValue"]',
                timeout=5000,
            )
            declaration_annotation_type_reference_detail = (
                "declaration annotation constructor type reference" in page.locator(
                    '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare annotation type PreviewValue Bind.Asset PreviewValue"] [data-graph-search-result-detail]'
                ).first.inner_text()
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare annotation type PreviewValue Bind.Asset PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_annotation_type_reference_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("declare annotation schema PreviewSchema.default_payload Bind.Asset PreviewValue")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare annotation schema PreviewSchema.default_payload Bind.Asset PreviewValue"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="source_reference"][data-graph-search-result-label="declare annotation schema PreviewSchema.default_payload Bind.Asset PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-source-sync-state="declaration"]', timeout=5000)
            page.wait_for_selector('[data-source-line="1"][data-source-focused="true"]', timeout=5000)
            declaration_annotation_schema_field_reference_highlighted = (
                page.locator('[data-source-preview-label="true"]').inner_text() == "custom.d.gs" and
                page.locator('[data-source-range="active"]').inner_text() == "PreviewValue"
            )

            search_box.fill("PreviewValue")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="PreviewValue"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="PreviewValue"]'
            ).click()
            page.wait_for_selector('[data-declaration-rename="true"]', timeout=5000)
            type_rename_input = page.locator('[data-declaration-rename-input="true"]')
            declaration_type_rename_input_prefilled = type_rename_input.input_value() == "PreviewValue"
            declaration_type_rename_kind = (
                page.locator('[data-declaration-rename="true"]').get_attribute("data-declaration-rename-kind") == "type"
            )
            type_rename_input.fill("RenamedValue")
            page.locator('[data-declaration-rename="true"] button[type="submit"]').click()
            page.locator('[data-source-sync-detail="true"]').filter(has_text="declpreview0002").wait_for(timeout=5000)
            declaration_type_rename_command_recorded = (
                "apply_import_type_rename custom.d.gs PreviewValue RenamedValue declpreview0001" in exec_commands
            )
            declaration_type_rename_source_updated = (
                page.locator('[data-source-line]', has_text="declare type RenamedValue;").count() == 1
            )
            declaration_type_rename_input_updated = (
                page.locator('[data-declaration-rename-input="true"]').input_value() == "RenamedValue"
            )

            search_box.fill("PreviewSchema.default_payload")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="PreviewSchema.default_payload"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="PreviewSchema.default_payload"]'
            ).click()
            page.wait_for_selector('[data-declaration-rename="true"]', timeout=5000)
            schema_field_rename_input = page.locator('[data-declaration-rename-input="true"]')
            declaration_schema_field_rename_input_prefilled = schema_field_rename_input.input_value() == "default_payload"
            declaration_schema_field_rename_kind = (
                page.locator('[data-declaration-rename="true"]').get_attribute("data-declaration-rename-kind") == "schema_field"
            )
            schema_field_rename_input.fill("fallback_payload")
            page.locator('[data-declaration-rename="true"] button[type="submit"]').click()
            page.locator('[data-source-sync-detail="true"]').filter(has_text="declpreview0003").wait_for(timeout=5000)
            declaration_schema_field_rename_command_recorded = (
                "apply_import_schema_field_rename custom.d.gs PreviewSchema default_payload fallback_payload declpreview0002" in exec_commands
            )
            declaration_schema_field_rename_source_updated = (
                page.locator('[data-source-line]', has_text="fallback_payload = RenamedValue(\"schema\");").count() == 1
            )
            declaration_schema_field_rename_input_updated = (
                page.locator('[data-declaration-rename-input="true"]').input_value() == "fallback_payload"
            )

            search_box.fill("PreviewSchema")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="PreviewSchema"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="PreviewSchema"]'
            ).click()
            page.wait_for_selector('[data-declaration-rename="true"]', timeout=5000)
            schema_rename_input = page.locator('[data-declaration-rename-input="true"]')
            declaration_schema_rename_input_prefilled = schema_rename_input.input_value() == "PreviewSchema"
            declaration_schema_rename_kind = (
                page.locator('[data-declaration-rename="true"]').get_attribute("data-declaration-rename-kind") == "schema"
            )
            schema_rename_input.fill("RenamedSchema")
            page.locator('[data-declaration-rename="true"] button[type="submit"]').click()
            page.locator('[data-source-sync-detail="true"]').filter(has_text="declpreview0004").wait_for(timeout=5000)
            declaration_schema_rename_command_recorded = (
                "apply_import_schema_rename custom.d.gs PreviewSchema RenamedSchema declpreview0003" in exec_commands
            )
            declaration_schema_rename_source_updated = (
                page.locator('[data-source-line]', has_text="declare Schema RenamedSchema").count() == 1
            )
            declaration_schema_rename_input_updated = (
                page.locator('[data-declaration-rename-input="true"]').input_value() == "RenamedSchema"
            )

            search_box.fill("CustomPreview.enter")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview.enter"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview.enter"]'
            ).click()
            page.wait_for_selector('[data-declaration-rename="true"]', timeout=5000)
            pin_rename_input = page.locator('[data-declaration-rename-input="true"]')
            declaration_pin_rename_input_prefilled = pin_rename_input.input_value() == "enter"
            declaration_pin_rename_kind = (
                page.locator('[data-declaration-rename="true"]').get_attribute("data-declaration-rename-kind") == "node_pin"
            )
            pin_rename_input.fill("begin")
            page.locator('[data-declaration-rename="true"] button[type="submit"]').click()
            page.locator('[data-source-sync-detail="true"]').filter(has_text="declpreview0005").wait_for(timeout=5000)
            declaration_pin_rename_command_recorded = (
                "apply_import_node_pin_rename custom.d.gs CustomPreview enter begin declpreview0004" in exec_commands
            )
            declaration_pin_rename_source_updated = (
                page.locator('[data-source-line]', has_text="exec in begin;").count() == 1
            )
            declaration_pin_rename_input_updated = (
                page.locator('[data-declaration-rename-input="true"]').input_value() == "begin"
            )

            search_box.fill("CustomPreview")
            page.wait_for_selector(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview"]',
                timeout=5000,
            )
            page.locator(
                '[data-graph-search-results] [data-graph-search-result-kind="declaration"][data-graph-search-result-label="CustomPreview"]'
            ).click()
            page.wait_for_selector('[data-declaration-rename="true"]', timeout=5000)
            rename_input = page.locator('[data-declaration-rename-input="true"]')
            declaration_rename_input_prefilled = rename_input.input_value() == "CustomPreview"
            rename_input.fill("RenamedPreview")
            page.locator('[data-declaration-rename="true"] button[type="submit"]').click()
            page.locator('[data-source-sync-detail="true"]').filter(has_text="declpreview0006").wait_for(timeout=5000)
            declaration_rename_command_recorded = (
                "apply_import_node_rename custom.d.gs CustomPreview RenamedPreview declpreview0005" in exec_commands
            )
            declaration_rename_source_updated = (
                page.locator('[data-source-line]', has_text="declare Node RenamedPreview").count() == 1
            )
            declaration_rename_input_updated = (
                page.locator('[data-declaration-rename-input="true"]').input_value() == "RenamedPreview"
            )

            page.screenshot(path=OUT / "declaration_source_preview.png", full_page=True)
            browser.close()

            results = [
                ("declaration preview label visible", declaration_label_visible),
                ("declaration content visible", declaration_content_visible),
                ("declaration edit disabled", declaration_edit_disabled),
                ("session source hidden while declaration open", session_source_hidden),
                ("session label restored", session_label_visible),
                ("session source restored", session_source_visible),
                ("session edit enabled", session_edit_enabled),
                ("declaration search finds node type", declaration_search_node_visible),
                ("declaration search node detail includes kind", declaration_search_node_detail),
                ("declaration search node highlights source", declaration_search_node_highlighted),
                ("declaration search finds pin", declaration_search_pin_visible),
                ("declaration search pin detail includes kind", declaration_search_pin_detail),
                ("declaration search pin highlights source", declaration_search_pin_highlighted),
                ("import declaration source opened", import_declaration_source_opened),
                ("import annotation constructor type definition highlighted", import_annotation_constructor_type_definition_highlighted),
                ("graph base schema definition highlighted", graph_base_schema_definition_highlighted),
                ("parameter type definition highlighted", parameter_type_definition_highlighted),
                ("parameter default constructor type definition highlighted", parameter_default_constructor_type_definition_highlighted),
                ("annotation constructor type definition highlighted", annotation_constructor_type_definition_highlighted),
                ("generate metadata constructor type definition highlighted", generate_metadata_constructor_type_definition_highlighted),
                ("generate metadata property definition highlighted", generate_metadata_property_definition_highlighted),
                ("generate metadata property type definition highlighted", generate_metadata_property_type_definition_highlighted),
                ("generate metadata annotation constructor type definition highlighted", generate_metadata_annotation_constructor_type_definition_highlighted),
                ("event annotation constructor type definition highlighted", event_annotation_constructor_type_definition_highlighted),
                ("flow annotation constructor type definition highlighted", flow_annotation_constructor_type_definition_highlighted),
                ("link annotation constructor type definition highlighted", link_annotation_constructor_type_definition_highlighted),
                ("let type definition highlighted", let_type_definition_highlighted),
                ("let constructor type definition highlighted", let_constructor_type_definition_highlighted),
                ("let annotation constructor type definition highlighted", let_annotation_constructor_type_definition_highlighted),
                ("declaration type name highlighted", declaration_type_name_highlighted),
                ("declaration type annotation constructor type definition highlighted", declaration_type_annotation_constructor_type_definition_highlighted),
                ("declaration type annotation constructor type source opened", declaration_type_annotation_constructor_type_source_opened),
                ("declaration node type name highlighted", declaration_node_type_name_highlighted),
                ("declaration node type annotation constructor type definition highlighted", declaration_node_type_annotation_constructor_type_definition_highlighted),
                ("declaration node type annotation constructor type source opened", declaration_node_type_annotation_constructor_type_source_opened),
                ("declaration node pin name highlighted", declaration_node_pin_name_highlighted),
                ("declaration node pin type highlighted", declaration_node_pin_type_highlighted),
                ("declaration node pin type definition highlighted", declaration_node_pin_type_definition_highlighted),
                ("declaration node pin annotation constructor type source opened", declaration_node_pin_annotation_constructor_type_source_opened),
                ("declaration schema name highlighted", declaration_schema_name_highlighted),
                ("declaration schema annotation constructor type definition highlighted", declaration_schema_annotation_constructor_type_definition_highlighted),
                ("declaration schema annotation constructor type source opened", declaration_schema_annotation_constructor_type_source_opened),
                ("declaration schema field value highlighted", declaration_schema_field_value_highlighted),
                ("declaration schema field constructor type highlighted", declaration_schema_field_constructor_type_highlighted),
                ("declaration schema field constructor type definition highlighted", declaration_schema_field_constructor_type_definition_highlighted),
                ("declaration schema field annotation constructor type definition highlighted", declaration_schema_field_annotation_constructor_type_definition_highlighted),
                ("declaration schema field annotation constructor type source opened", declaration_schema_field_annotation_constructor_type_source_opened),
                ("flow source pin definition highlighted", flow_source_pin_definition_highlighted),
                ("flow target pin definition highlighted", flow_target_pin_definition_highlighted),
                ("link source parameter type definition highlighted", link_source_parameter_type_definition_highlighted),
                ("link target pin definition highlighted", link_target_pin_definition_highlighted),
                ("link target pin type definition highlighted", link_target_pin_type_definition_highlighted),
                ("link source pin definition highlighted", link_source_pin_definition_highlighted),
                ("link source pin type definition highlighted", link_source_pin_type_definition_highlighted),
                ("node type definition from declaration highlighted", node_type_definition_from_declaration),
                ("selected node type annotation constructor type source opened", selected_node_type_annotation_constructor_type_source_opened),
                ("node pin name from declaration highlighted", node_pin_name_from_declaration),
                ("node pin type definition highlighted", node_pin_type_definition_highlighted),
                ("selected node pin annotation constructor type source opened", selected_node_pin_annotation_constructor_type_source_opened),
                ("initializer field name definition highlighted", initializer_field_name_definition_highlighted),
                ("initializer field name type definition highlighted", initializer_field_name_type_definition_highlighted),
                ("initializer field constructor type definition highlighted", initializer_field_constructor_type_definition_highlighted),
                ("raw initializer constructor type definition highlighted", raw_initializer_constructor_type_definition_highlighted),
                ("graph search source highlighted", graph_search_source_highlighted),
                ("node search source highlighted", node_search_source_highlighted),
                ("block search source highlighted", block_search_source_highlighted),
                ("inline flow delete command recorded", inline_flow_delete_command_recorded),
                ("inline link delete command recorded", inline_link_delete_command_recorded),
                ("source reference parameter detail", source_reference_param_detail),
                ("source reference parameter constructor highlighted", source_reference_param_constructor_highlighted),
                ("source reference range parameter detail", source_reference_range_param_detail),
                ("source reference range parameter highlighted", source_reference_range_param_highlighted),
                ("source reference raw initializer highlighted", source_reference_raw_initializer_highlighted),
                ("source reference flow detail", source_reference_flow_detail),
                ("source reference flow endpoint highlighted", source_reference_flow_endpoint_highlighted),
                ("source reference link endpoint highlighted", source_reference_link_endpoint_highlighted),
                ("declaration source reference pin detail", declaration_source_reference_pin_detail),
                ("declaration source reference pin type highlighted", declaration_source_reference_pin_type_highlighted),
                ("declaration source reference file range detail", declaration_source_reference_file_range_detail),
                ("declaration source reference file range highlighted", declaration_source_reference_file_range_highlighted),
                ("declaration source reference file point detail", declaration_source_reference_file_point_detail),
                ("declaration source reference file point highlighted", declaration_source_reference_file_point_highlighted),
                ("declaration source reference paren point detail", declaration_source_reference_paren_point_detail),
                ("declaration source reference paren point highlighted", declaration_source_reference_paren_point_highlighted),
                ("declaration source reference log line detail", declaration_source_reference_log_line_detail),
                ("declaration source reference log line highlighted", declaration_source_reference_log_line_highlighted),
                ("declaration source reference path log line detail", declaration_source_reference_path_log_line_detail),
                ("declaration source reference path log line highlighted", declaration_source_reference_path_log_line_highlighted),
                ("declaration source reference quoted path log line detail", declaration_source_reference_quoted_path_log_line_detail),
                ("declaration source reference quoted path log line highlighted", declaration_source_reference_quoted_path_log_line_highlighted),
                ("declaration source reference single quoted path log line detail", declaration_source_reference_single_quoted_path_log_line_detail),
                ("declaration source reference single quoted path log line highlighted", declaration_source_reference_single_quoted_path_log_line_highlighted),
                ("declaration source reference file line detail", declaration_source_reference_file_line_detail),
                ("declaration source reference file line highlighted", declaration_source_reference_file_line_highlighted),
                ("declaration source reference hash line detail", declaration_source_reference_hash_line_detail),
                ("declaration source reference hash line highlighted", declaration_source_reference_hash_line_highlighted),
                ("declaration source reference hash line range detail", declaration_source_reference_hash_line_range_detail),
                ("declaration source reference hash line range highlighted", declaration_source_reference_hash_line_range_highlighted),
                ("declaration source reference schema field constructor highlighted", declaration_source_reference_schema_field_constructor_highlighted),
                ("declaration annotation type reference detail", declaration_annotation_type_reference_detail),
                ("declaration annotation type reference highlighted", declaration_annotation_type_reference_highlighted),
                ("declaration annotation schema field reference highlighted", declaration_annotation_schema_field_reference_highlighted),
                ("declaration type rename input prefilled", declaration_type_rename_input_prefilled),
                ("declaration type rename kind recorded", declaration_type_rename_kind),
                ("declaration type rename command recorded", declaration_type_rename_command_recorded),
                ("declaration type rename source updated", declaration_type_rename_source_updated),
                ("declaration type rename input updated", declaration_type_rename_input_updated),
                ("declaration schema field rename input prefilled", declaration_schema_field_rename_input_prefilled),
                ("declaration schema field rename kind recorded", declaration_schema_field_rename_kind),
                ("declaration schema field rename command recorded", declaration_schema_field_rename_command_recorded),
                ("declaration schema field rename source updated", declaration_schema_field_rename_source_updated),
                ("declaration schema field rename input updated", declaration_schema_field_rename_input_updated),
                ("declaration schema rename input prefilled", declaration_schema_rename_input_prefilled),
                ("declaration schema rename kind recorded", declaration_schema_rename_kind),
                ("declaration schema rename command recorded", declaration_schema_rename_command_recorded),
                ("declaration schema rename source updated", declaration_schema_rename_source_updated),
                ("declaration schema rename input updated", declaration_schema_rename_input_updated),
                ("declaration pin rename input prefilled", declaration_pin_rename_input_prefilled),
                ("declaration pin rename kind recorded", declaration_pin_rename_kind),
                ("declaration pin rename command recorded", declaration_pin_rename_command_recorded),
                ("declaration pin rename source updated", declaration_pin_rename_source_updated),
                ("declaration pin rename input updated", declaration_pin_rename_input_updated),
                ("declaration rename input prefilled", declaration_rename_input_prefilled),
                ("declaration rename command recorded", declaration_rename_command_recorded),
                ("declaration rename source updated", declaration_rename_source_updated),
                ("declaration rename input updated", declaration_rename_input_updated),
            ]
    finally:
        stop_process_tree(proc)

    failed = [name for name, ok in results if not ok]
    print("\nDeclaration source preview smoke")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'} {name}")
    print(f"Screenshots: {OUT}")
    if failed:
        raise SystemExit("Failed checks: " + ", ".join(failed))


if __name__ == "__main__":
    sys.exit(main())
