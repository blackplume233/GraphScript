// Task editor node declarations
export declare object TaskStart {
    @flow.pin(kind = "exec", direction = "out")
    begin: Exec;
}

export declare object TaskComplete {
    @flow.pin(kind = "exec", direction = "in")
    finish: Exec;
    @flow.input
    result: bool;
}

export declare object ShowDialogue {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    text: FString;
    @flow.input
    speaker: FName;
}

export declare schema TaskGraph: FlowGraphSchema {
    max_exec_fan_out: 1;
    allow_exec_fan_in: true;
    strict_type_match: false;
    allowed_node_tags: ["task", "common"];
}
