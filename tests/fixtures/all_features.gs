// Scenario 7: Exercises every language feature in one file

import "ue_core.d.gs";
import "htn_nodes.d.gs";

let constant_path = SoftObjectPath("/Game/Maps/TestLevel");
let spawn_point = FVector("0,0,100");

[Comment("title", "Kitchen sink test - all features"), Comment("note", "Second comment")]
Graph KitchenSink {
    in health : int;
    in name : FString;
    out alive : bool;
    out score : int;
    var temp_buffer : float;

    [Position(X = 100, Y = 200)]
    PrintString logger1{};
    [Position(X = 300, Y = 200)]
    PrintString logger2{};
    [Position(X = 200, Y = 100)]
    Delay timer{};
    [Position(X = 100, Y = 400)]
    GetActorLocation locator{};

    event OnStart {
        context.start(logger1.enter);
        logger1.exit(timer.enter);
        timer.completed(logger2.enter);
        link logger1.message = name;
        link timer.duration = temp_buffer;
        link logger2.message = health;
    }

    event OnDamage {
        context.start(logger2.enter);
        link logger2.message = health;
    }

    function CalculateScore {
        context.start(context.done);
        link context.result = score;
    }

    function LogLocation {
        context.start(context.done);
        link context.result = name;
    }
}
