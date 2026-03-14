// Level script node declarations
declare Node TriggerVolume {
    exec out onEnter;
    exec out onExit;
    data in volume : AActor;
}

declare Node SpawnActor {
    exec in enter;
    exec out exit;
    data in actorClass : FName;
    data in location : FVector;
    data out spawned : AActor;
}

declare Schema LevelScriptGraph {
    max_exec_fan_out: unlimited;
    allow_exec_fan_in: true;
    strict_type_match: false;
    allowed_node_tags: ["level", "common"];
}
