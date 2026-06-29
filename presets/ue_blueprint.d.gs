// Extended UE Blueprint node library
// Covers: Math, Logic, Flow Control, Actor, Component, Physics,
//         Gameplay, UI, Spawning, Timer, Debug

// ─── Additional Types ──────────────────────────────────────────────
declare type FVector2D : constructible;
declare type FLinearColor : constructible;
declare type FTransform : constructible;
declare type FHitResult;
declare type UClass;
declare type USceneComponent;
declare type UStaticMeshComponent;
declare type UWidgetComponent;
declare type UUserWidget;
declare type USoundBase;
declare type UParticleSystem;
declare type UAnimMontage;
declare type UDamageType;
declare type ACharacter;
declare type APlayerController;
declare type APawn;
declare type AProjectile;
declare type UPrimitiveComponent;

// ─── Math Nodes ────────────────────────────────────────────────────
declare Node Add_Float {
    data in A : float;
    data in B : float;
    data out Result : float;
}

declare Node Subtract_Float {
    data in A : float;
    data in B : float;
    data out Result : float;
}

declare Node Multiply_Float {
    data in A : float;
    data in B : float;
    data out Result : float;
}

declare Node Divide_Float {
    data in A : float;
    data in B : float;
    data out Result : float;
}

declare Node Clamp_Float {
    data in Value : float;
    data in Min : float;
    data in Max : float;
    data out Result : float;
}

declare Node Lerp_Float {
    data in A : float;
    data in B : float;
    data in Alpha : float;
    data out Result : float;
}

declare Node Add_Vector {
    data in A : FVector;
    data in B : FVector;
    data out Result : FVector;
}

declare Node Subtract_Vector {
    data in A : FVector;
    data in B : FVector;
    data out Result : FVector;
}

declare Node Multiply_VectorFloat {
    data in Vec : FVector;
    data in Scale : float;
    data out Result : FVector;
}

declare Node VectorLength {
    data in Vec : FVector;
    data out Length : float;
}

declare Node Normalize_Vector {
    data in Vec : FVector;
    data out Result : FVector;
}

declare Node MakeVector {
    data in X : float;
    data in Y : float;
    data in Z : float;
    data out Result : FVector;
}

declare Node BreakVector {
    data in Vec : FVector;
    data out X : float;
    data out Y : float;
    data out Z : float;
}

declare Node RandomFloatInRange {
    data in Min : float;
    data in Max : float;
    data out Result : float;
}

// ─── Comparison / Logic Nodes ──────────────────────────────────────
declare Node Compare_Float {
    data in A : float;
    data in B : float;
    data out IsGreater : bool;
    data out IsEqual : bool;
    data out IsLess : bool;
}

declare Node BoolAnd {
    data in A : bool;
    data in B : bool;
    data out Result : bool;
}

declare Node BoolOr {
    data in A : bool;
    data in B : bool;
    data out Result : bool;
}

declare Node BoolNot {
    data in Value : bool;
    data out Result : bool;
}

// ─── Flow Control ──────────────────────────────────────────────────
declare Node Branch {
    exec in enter;
    exec out onTrue;
    exec out onFalse;
    data in condition : bool;
}

declare Node Sequence {
    exec in enter;
    exec out then0;
    exec out then1;
    exec out then2;
    exec out then3;
}

declare Node ForLoop {
    exec in enter;
    exec out loopBody;
    exec out completed;
    data in firstIndex : int;
    data in lastIndex : int;
    data out index : int;
}

declare Node Gate {
    exec in enter;
    exec in open;
    exec in close;
    exec out exit;
}

declare Node DoOnce {
    exec in enter;
    exec in reset;
    exec out completed;
}

declare Node FlipFlop {
    exec in enter;
    exec out outA;
    exec out outB;
    data out isA : bool;
}

declare Node Select_Float {
    data in condition : bool;
    data in onTrue : float;
    data in onFalse : float;
    data out Result : float;
}

// ─── Actor / Component ────────────────────────────────────────────
declare Node GetActorRotation {
    data in target : AActor;
    data out rotation : FRotator;
}

declare Node SetActorLocation {
    exec in enter;
    exec out exit;
    data in target : AActor;
    data in newLocation : FVector;
    data in sweep : bool;
}

declare Node SetActorRotation {
    exec in enter;
    exec out exit;
    data in target : AActor;
    data in newRotation : FRotator;
}

declare Node GetActorForwardVector {
    data in target : AActor;
    data out forward : FVector;
}

declare Node GetDistanceTo {
    data in from : AActor;
    data in to : AActor;
    data out distance : float;
}

declare Node IsValid {
    data in object : UObject;
    data out valid : bool;
}

declare Node DestroyActor {
    exec in enter;
    exec out exit;
    data in target : AActor;
}

declare Node GetPlayerCharacter {
    data out character : ACharacter;
}

declare Node GetPlayerController {
    data out controller : APlayerController;
}

// ─── Movement / Physics ───────────────────────────────────────────
declare Node AddMovementInput {
    exec in enter;
    exec out exit;
    data in target : ACharacter;
    data in direction : FVector;
    data in scale : float;
}

declare Node LaunchCharacter {
    exec in enter;
    exec out exit;
    data in target : ACharacter;
    data in velocity : FVector;
    data in overrideXY : bool;
    data in overrideZ : bool;
}

declare Node LineTraceByChannel {
    exec in enter;
    exec out hit;
    exec out noHit;
    data in start : FVector;
    data in end : FVector;
    data out hitResult : FHitResult;
    data out hitLocation : FVector;
    data out hitNormal : FVector;
}

declare Node SphereOverlap {
    exec in enter;
    exec out exit;
    data in center : FVector;
    data in radius : float;
    data out hitCount : int;
}

// ─── Damage / Health ──────────────────────────────────────────────
declare Node ApplyDamage {
    exec in enter;
    exec out exit;
    data in target : AActor;
    data in damage : float;
    data in instigator : APlayerController;
    data in damageType : UDamageType;
}

declare Node ApplyRadialDamage {
    exec in enter;
    exec out exit;
    data in origin : FVector;
    data in radius : float;
    data in baseDamage : float;
    data in instigator : APlayerController;
}

// ─── Spawn / Destroy ──────────────────────────────────────────────
declare Node SpawnActorFromClass {
    exec in enter;
    exec out exit;
    data in actorClass : UClass;
    data in location : FVector;
    data in rotation : FRotator;
    data out spawnedActor : AActor;
}

declare Node SpawnEmitterAtLocation {
    exec in enter;
    exec out exit;
    data in particle : UParticleSystem;
    data in location : FVector;
    data in rotation : FRotator;
}

declare Node PlaySoundAtLocation {
    exec in enter;
    exec out exit;
    data in sound : USoundBase;
    data in location : FVector;
    data in volume : float;
}

// ─── Timer ────────────────────────────────────────────────────────
declare Node SetTimer {
    exec in enter;
    exec out exit;
    exec out onTimer;
    data in time : float;
    data in looping : bool;
}

declare Node ClearTimer {
    exec in enter;
    exec out exit;
}

// ─── Animation ────────────────────────────────────────────────────
declare Node PlayAnimMontage {
    exec in enter;
    exec out exit;
    exec out onCompleted;
    exec out onInterrupted;
    data in target : ACharacter;
    data in montage : UAnimMontage;
    data in playRate : float;
}

// ─── UI ───────────────────────────────────────────────────────────
declare Node CreateWidget {
    exec in enter;
    exec out exit;
    data in widgetClass : UClass;
    data in owner : APlayerController;
    data out widget : UUserWidget;
}

declare Node AddToViewport {
    exec in enter;
    exec out exit;
    data in widget : UUserWidget;
    data in zOrder : int;
}

declare Node RemoveFromParent {
    exec in enter;
    exec out exit;
    data in widget : UUserWidget;
}

declare Node SetTextBlock {
    exec in enter;
    exec out exit;
    data in widget : UUserWidget;
    data in text : FString;
}

declare Node SetProgressBar {
    exec in enter;
    exec out exit;
    data in widget : UUserWidget;
    data in percent : float;
}

declare Node SetVisibility {
    exec in enter;
    exec out exit;
    data in widget : UUserWidget;
    data in visible : bool;
}

// ─── Debug ────────────────────────────────────────────────────────
declare Node DrawDebugLine {
    exec in enter;
    exec out exit;
    data in start : FVector;
    data in end : FVector;
    field duration : float = 0.0;
    data in duration : float;
}

declare Node DrawDebugSphere {
    exec in enter;
    exec out exit;
    data in center : FVector;
    data in radius : float;
    field duration : float = 0.0;
    data in duration : float;
}

// ─── Conversion ───────────────────────────────────────────────────
declare Node FloatToString {
    data in value : float;
    data out result : FString;
}

declare Node IntToString {
    data in value : int;
    data out result : FString;
}

declare Node AppendStrings {
    data in A : FString;
    data in B : FString;
    data out Result : FString;
}
