// HTN domain node declarations
declare type HTNTask;
declare type HTNCondition;

declare Node HTN_MoveToTarget {
    exec in enter;
    exec out success;
    exec out fail;
    data in target : AActor;
    data in speed : float;
}

declare Node HTN_CheckDistance {
    exec in enter;
    exec out inRange;
    exec out outOfRange;
    data in target : AActor;
    data in threshold : float;
}

declare Schema HTNGraph {
    max_exec_fan_out: unlimited;
    allow_exec_fan_in: false;
    strict_type_match: true;
    allowed_node_tags: ["htn_task", "htn_decorator", "htn_service", "common"];
}
