// Scenario 8: Complex .d.gs with many declarations of mixed types

declare type GameplayTag;
declare type FTransform : constructible;
declare type FLinearColor : constructible;
declare type UAnimMontage;
declare type USoundBase;
declare type UParticleSystem;

declare Node PlayMontage {
    exec in play;
    exec out completed;
    exec out interrupted;
    data in target : AActor;
    data in montage : UAnimMontage;
    data in playRate : float;
}

declare Node PlaySound {
    exec in play;
    exec out finished;
    data in sound : USoundBase;
    data in location : FVector;
    data in volume : float;
}

declare Node SpawnParticle {
    exec in spawn;
    exec out done;
    data in particle : UParticleSystem;
    data in transform : FTransform;
    data in color : FLinearColor;
}

declare Node BranchOnTag {
    exec in enter;
    exec out matched;
    exec out notMatched;
    data in tag : GameplayTag;
    data in target : AActor;
}

declare Schema CinematicGraph {
    max_exec_fan_out: 1;
    allow_exec_fan_in: true;
    strict_type_match: true;
    allowed_node_tags: ["cinematic", "common"];
}

declare Schema AbilityGraph {
    max_exec_fan_out: unlimited;
    allow_exec_fan_in: false;
    strict_type_match: false;
    allowed_node_tags: ["ability", "common"];
}
