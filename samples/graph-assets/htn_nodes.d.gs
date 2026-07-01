// HTN domain node declarations
export declare type HTNTask;
export declare type HTNCondition;

export declare object HTN_MoveToTarget {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    success: Exec;
    @flow.pin(kind = "exec", direction = "out")
    fail: Exec;
    @flow.input
    target: AActor;
    @flow.input
    speed: float;
}

export declare object HTN_CheckDistance {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    inRange: Exec;
    @flow.pin(kind = "exec", direction = "out")
    outOfRange: Exec;
    @flow.input
    target: AActor;
    @flow.input
    threshold: float;
}

export declare schema HTNGraph: FlowGraphSchema {
    max_exec_fan_out: unlimited;
    allow_exec_fan_in: false;
    strict_type_match: true;
    allowed_node_tags: ["htn_task", "htn_decorator", "htn_service", "common"];
}
