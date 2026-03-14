// UE Core type declarations
declare type FName;
declare type FString;
declare type FVector : constructible;
declare type FRotator : constructible;
declare type SoftObjectPath : constructible;
declare type AActor;
declare type UObject;
declare type float;
declare type int;
declare type bool;

declare Node PrintString {
    exec in enter;
    exec out exit;
    data in message : FString;
}

declare Node Delay {
    exec in enter;
    exec out completed;
    data in duration : float;
}

declare Node GetActorLocation {
    data in target : AActor;
    data out location : FVector;
}
