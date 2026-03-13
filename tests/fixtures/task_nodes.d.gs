// Task editor node declarations
declare Node TaskStart {
    exec out begin;
}

declare Node TaskComplete {
    exec in finish;
    data in result : bool;
}

declare Node ShowDialogue {
    exec in enter;
    exec out exit;
    data in text : FString;
    data in speaker : FName;
}

declare Schema TaskGraph {
    max_exec_fan_out: 1;
    allow_exec_fan_in: true;
    strict_type_match: false;
    allowed_node_tags: ["task", "common"];
}
