// Level script node declarations
export declare object TriggerVolume {
    @flow.pin(kind = "exec", direction = "out")
    onEnter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    onExit: Exec;
    @flow.input
    volume: AActor;
}

export declare object SpawnActor {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    actorClass: FName;
    @flow.input
    location: FVector;
    @flow.output
    spawned: AActor;
}

export declare schema LevelScriptGraph: FlowGraphSchema {
    max_exec_fan_out: unlimited;
    allow_exec_fan_in: true;
    strict_type_match: false;
    allowed_node_tags: ["level", "common"];
}
