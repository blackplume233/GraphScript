// UE Core type declarations
export declare type FName;
export declare type FString;
export declare type FVector: constructible;
export declare type FRotator: constructible;
export declare type SoftObjectPath: constructible;
export declare type AActor;
export declare type UObject;
export declare type float;
export declare type int;
export declare type bool;
export declare type Exec;

export declare object PrintString {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    message: FString = "";
    @flow.input
    message: FString;
}

export declare object Delay {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    completed: Exec;
    duration: float = 0.2;
    @flow.input
    duration: float;
}

export declare object GetActorLocation {
    @flow.input
    target: AActor;
    @flow.output
    location: FVector;
}
