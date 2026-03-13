// Scenario 7: Exercises every language feature in one file

import "ue_core.d.gs";
import "htn_nodes.d.gs";

let constant_path = SoftObjectPath("/Game/Maps/TestLevel");
let spawn_point = FVector("0,0,100");

Graph KitchenSink {
    in health : int;
    in name : FString;
    out alive : bool;
    out score : int;
    var temp_buffer : float;

    PrintString logger1{};
    PrintString logger2{};
    Delay timer{};
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
        context.start(logger1.enter);
        link logger1.message = score;
    }

    function LogLocation {
        context.start(logger2.enter);
        link logger2.message = name;
    }

    generate {
        Comment title = "Kitchen sink test - all features";
        Comment note = "Second comment";
        position:logger1.x(100);
        position:logger1.y(200);
        position:logger2.x(300);
        position:logger2.y(200);
        position:timer.x(200);
        position:timer.y(100);
        position:locator.x(100);
        position:locator.y(400);
    }
}
