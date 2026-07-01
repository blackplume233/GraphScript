// Scenario 8: Complex .d.gs with many declarations of mixed types

export declare type GameplayTag;
export declare type FTransform: constructible;
export declare type FLinearColor: constructible;
export declare type UAnimMontage;
export declare type USoundBase;
export declare type UParticleSystem;

export declare object PlayMontage {
    @flow.pin(kind = "exec", direction = "in")
    play: Exec;
    @flow.pin(kind = "exec", direction = "out")
    completed: Exec;
    @flow.pin(kind = "exec", direction = "out")
    interrupted: Exec;
    @flow.input
    target: AActor;
    @flow.input
    montage: UAnimMontage;
    @flow.input
    playRate: float;
}

export declare object PlaySound {
    @flow.pin(kind = "exec", direction = "in")
    play: Exec;
    @flow.pin(kind = "exec", direction = "out")
    finished: Exec;
    @flow.input
    sound: USoundBase;
    @flow.input
    location: FVector;
    @flow.input
    volume: float;
}

export declare object SpawnParticle {
    @flow.pin(kind = "exec", direction = "in")
    spawn: Exec;
    @flow.pin(kind = "exec", direction = "out")
    done: Exec;
    @flow.input
    particle: UParticleSystem;
    @flow.input
    transform: FTransform;
    @flow.input
    color: FLinearColor;
}

export declare object BranchOnTag {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    matched: Exec;
    @flow.pin(kind = "exec", direction = "out")
    notMatched: Exec;
    @flow.input
    tag: GameplayTag;
    @flow.input
    target: AActor;
}

export declare schema CinematicGraph: FlowGraphSchema {
    max_exec_fan_out: 1;
    allow_exec_fan_in: true;
    strict_type_match: true;
    allowed_node_tags: ["cinematic", "common"];
}

export declare schema AbilityGraph: FlowGraphSchema {
    max_exec_fan_out: unlimited;
    allow_exec_fan_in: false;
    strict_type_match: false;
    allowed_node_tags: ["ability", "common"];
}
