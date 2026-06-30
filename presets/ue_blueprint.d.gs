// Extended UE Blueprint node library
// Covers: Math, Logic, Flow Control, Actor, Component, Physics,
//         Gameplay, UI, Spawning, Timer, Debug

// ─── Additional Types ──────────────────────────────────────────────
export declare type FVector2D: constructible;
export declare type FLinearColor: constructible;
export declare type FTransform: constructible;
export declare type FHitResult;
export declare type UClass;
export declare type USceneComponent;
export declare type UStaticMeshComponent;
export declare type UWidgetComponent;
export declare type UUserWidget;
export declare type USoundBase;
export declare type UParticleSystem;
export declare type UAnimMontage;
export declare type UDamageType;
export declare type ACharacter;
export declare type APlayerController;
export declare type APawn;
export declare type AProjectile;
export declare type UPrimitiveComponent;

// ─── Math Nodes ────────────────────────────────────────────────────
export declare object Add_Float {
    @flow.input
    A: float;
    @flow.input
    B: float;
    @flow.output
    Result: float;
}

export declare object Subtract_Float {
    @flow.input
    A: float;
    @flow.input
    B: float;
    @flow.output
    Result: float;
}

export declare object Multiply_Float {
    @flow.input
    A: float;
    @flow.input
    B: float;
    @flow.output
    Result: float;
}

export declare object Divide_Float {
    @flow.input
    A: float;
    @flow.input
    B: float;
    @flow.output
    Result: float;
}

export declare object Clamp_Float {
    @flow.input
    Value: float;
    @flow.input
    Min: float;
    @flow.input
    Max: float;
    @flow.output
    Result: float;
}

export declare object Lerp_Float {
    @flow.input
    A: float;
    @flow.input
    B: float;
    @flow.input
    Alpha: float;
    @flow.output
    Result: float;
}

export declare object Add_Vector {
    @flow.input
    A: FVector;
    @flow.input
    B: FVector;
    @flow.output
    Result: FVector;
}

export declare object Subtract_Vector {
    @flow.input
    A: FVector;
    @flow.input
    B: FVector;
    @flow.output
    Result: FVector;
}

export declare object Multiply_VectorFloat {
    @flow.input
    Vec: FVector;
    @flow.input
    Scale: float;
    @flow.output
    Result: FVector;
}

export declare object VectorLength {
    @flow.input
    Vec: FVector;
    @flow.output
    Length: float;
}

export declare object Normalize_Vector {
    @flow.input
    Vec: FVector;
    @flow.output
    Result: FVector;
}

export declare object MakeVector {
    @flow.input
    X: float;
    @flow.input
    Y: float;
    @flow.input
    Z: float;
    @flow.output
    Result: FVector;
}

export declare object BreakVector {
    @flow.input
    Vec: FVector;
    @flow.output
    X: float;
    @flow.output
    Y: float;
    @flow.output
    Z: float;
}

export declare object RandomFloatInRange {
    @flow.input
    Min: float;
    @flow.input
    Max: float;
    @flow.output
    Result: float;
}

// ─── Comparison / Logic Nodes ──────────────────────────────────────
export declare object Compare_Float {
    @flow.input
    A: float;
    @flow.input
    B: float;
    @flow.output
    IsGreater: bool;
    @flow.output
    IsEqual: bool;
    @flow.output
    IsLess: bool;
}

export declare object BoolAnd {
    @flow.input
    A: bool;
    @flow.input
    B: bool;
    @flow.output
    Result: bool;
}

export declare object BoolOr {
    @flow.input
    A: bool;
    @flow.input
    B: bool;
    @flow.output
    Result: bool;
}

export declare object BoolNot {
    @flow.input
    Value: bool;
    @flow.output
    Result: bool;
}

// ─── Flow Control ──────────────────────────────────────────────────
export declare object Branch {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    onTrue: Exec;
    @flow.pin(kind = "exec", direction = "out")
    onFalse: Exec;
    @flow.input
    condition: bool;
}

export declare object Sequence {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    then0: Exec;
    @flow.pin(kind = "exec", direction = "out")
    then1: Exec;
    @flow.pin(kind = "exec", direction = "out")
    then2: Exec;
    @flow.pin(kind = "exec", direction = "out")
    then3: Exec;
}

export declare object ForLoop {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    loopBody: Exec;
    @flow.pin(kind = "exec", direction = "out")
    completed: Exec;
    @flow.input
    firstIndex: int;
    @flow.input
    lastIndex: int;
    @flow.output
    index: int;
}

export declare object Gate {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "in")
    open: Exec;
    @flow.pin(kind = "exec", direction = "in")
    close: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
}

export declare object DoOnce {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "in")
    reset: Exec;
    @flow.pin(kind = "exec", direction = "out")
    completed: Exec;
}

export declare object FlipFlop {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    outA: Exec;
    @flow.pin(kind = "exec", direction = "out")
    outB: Exec;
    @flow.output
    isA: bool;
}

export declare object Select_Float {
    @flow.input
    condition: bool;
    @flow.input
    onTrue: float;
    @flow.input
    onFalse: float;
    @flow.output
    Result: float;
}

// ─── Actor / Component ────────────────────────────────────────────
export declare object GetActorRotation {
    @flow.input
    target: AActor;
    @flow.output
    rotation: FRotator;
}

export declare object SetActorLocation {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    target: AActor;
    @flow.input
    newLocation: FVector;
    @flow.input
    sweep: bool;
}

export declare object SetActorRotation {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    target: AActor;
    @flow.input
    newRotation: FRotator;
}

export declare object GetActorForwardVector {
    @flow.input
    target: AActor;
    @flow.output
    forward: FVector;
}

export declare object GetDistanceTo {
    @flow.input
    from: AActor;
    @flow.input
    to: AActor;
    @flow.output
    distance: float;
}

export declare object IsValid {
    @flow.input
    object: UObject;
    @flow.output
    valid: bool;
}

export declare object DestroyActor {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    target: AActor;
}

export declare object GetPlayerCharacter {
    @flow.output
    character: ACharacter;
}

export declare object GetPlayerController {
    @flow.output
    controller: APlayerController;
}

// ─── Movement / Physics ───────────────────────────────────────────
export declare object AddMovementInput {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    target: ACharacter;
    @flow.input
    direction: FVector;
    @flow.input
    scale: float;
}

export declare object LaunchCharacter {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    target: ACharacter;
    @flow.input
    velocity: FVector;
    @flow.input
    overrideXY: bool;
    @flow.input
    overrideZ: bool;
}

export declare object LineTraceByChannel {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    hit: Exec;
    @flow.pin(kind = "exec", direction = "out")
    noHit: Exec;
    @flow.input
    start: FVector;
    @flow.input
    end: FVector;
    @flow.output
    hitResult: FHitResult;
    @flow.output
    hitLocation: FVector;
    @flow.output
    hitNormal: FVector;
}

export declare object SphereOverlap {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    center: FVector;
    @flow.input
    radius: float;
    @flow.output
    hitCount: int;
}

// ─── Damage / Health ──────────────────────────────────────────────
export declare object ApplyDamage {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    target: AActor;
    @flow.input
    damage: float;
    @flow.input
    instigator: APlayerController;
    @flow.input
    damageType: UDamageType;
}

export declare object ApplyRadialDamage {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    origin: FVector;
    @flow.input
    radius: float;
    @flow.input
    baseDamage: float;
    @flow.input
    instigator: APlayerController;
}

// ─── Spawn / Destroy ──────────────────────────────────────────────
export declare object SpawnActorFromClass {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    actorClass: UClass;
    @flow.input
    location: FVector;
    @flow.input
    rotation: FRotator;
    @flow.output
    spawnedActor: AActor;
}

export declare object SpawnEmitterAtLocation {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    particle: UParticleSystem;
    @flow.input
    location: FVector;
    @flow.input
    rotation: FRotator;
}

export declare object PlaySoundAtLocation {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    sound: USoundBase;
    @flow.input
    location: FVector;
    @flow.input
    volume: float;
}

// ─── Timer ────────────────────────────────────────────────────────
export declare object SetTimer {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.pin(kind = "exec", direction = "out")
    onTimer: Exec;
    @flow.input
    time: float;
    @flow.input
    looping: bool;
}

export declare object ClearTimer {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
}

// ─── Animation ────────────────────────────────────────────────────
export declare object PlayAnimMontage {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.pin(kind = "exec", direction = "out")
    onCompleted: Exec;
    @flow.pin(kind = "exec", direction = "out")
    onInterrupted: Exec;
    @flow.input
    target: ACharacter;
    @flow.input
    montage: UAnimMontage;
    @flow.input
    playRate: float;
}

// ─── UI ───────────────────────────────────────────────────────────
export declare object CreateWidget {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    widgetClass: UClass;
    @flow.input
    owner: APlayerController;
    @flow.output
    widget: UUserWidget;
}

export declare object AddToViewport {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    widget: UUserWidget;
    @flow.input
    zOrder: int;
}

export declare object RemoveFromParent {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    widget: UUserWidget;
}

export declare object SetTextBlock {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    widget: UUserWidget;
    @flow.input
    text: FString;
}

export declare object SetProgressBar {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    widget: UUserWidget;
    @flow.input
    percent: float;
}

export declare object SetVisibility {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    widget: UUserWidget;
    @flow.input
    visible: bool;
}

// ─── Debug ────────────────────────────────────────────────────────
export declare object DrawDebugLine {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    start: FVector;
    @flow.input
    end: FVector;
    duration: float = 0.0;
    @flow.input
    duration: float;
}

export declare object DrawDebugSphere {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    center: FVector;
    @flow.input
    radius: float;
    duration: float = 0.0;
    @flow.input
    duration: float;
}

// ─── Conversion ───────────────────────────────────────────────────
export declare object FloatToString {
    @flow.input
    value: float;
    @flow.output
    result: FString;
}

export declare object IntToString {
    @flow.input
    value: int;
    @flow.output
    result: FString;
}

export declare object AppendStrings {
    @flow.input
    A: FString;
    @flow.input
    B: FString;
    @flow.output
    Result: FString;
}
