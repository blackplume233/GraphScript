// Scenario 7: Exercises every language feature in one file

import "ue_core.d.gs";
import "htn_nodes.d.gs";

const constant_path = new SoftObjectPath {
    path: "/Game/Maps/TestLevel";
};
const spawn_point = new FVector {
    value: "0,0,100";
};
@Comment("title", "Kitchen sink test - all features")
@Comment("note", "Second comment")
graph KitchenSink {
    @graph.input
    param health: int;
    @graph.input
    param name: FString;
    @graph.output
    param alive: bool;
    @graph.output
    param score: int;
    @graph.var
    param temp_buffer: float;
    @Position(X = 100, Y = 200)
    node logger1 {
        type PrintString;
    }
    @Position(X = 300, Y = 200)
    node logger2 {
        type PrintString;
    }
    @Position(X = 200, Y = 100)
    node timer {
        type Delay;
    }
    @Position(X = 100, Y = 400)
    node locator {
        type GetActorLocation;
    }
    event OnStart {
        connect(context.start, logger1.enter);
        connect(logger1.exit, timer.enter);
        connect(timer.completed, logger2.enter);
        bind(name, logger1.message);
        bind(temp_buffer, timer.duration);
        bind(health, logger2.message);
    }

    event OnDamage {
        connect(context.start, logger2.enter);
        bind(health, logger2.message);
    }

    function CalculateScore {
        connect(context.start, context.done);
        bind(score, context.result);
    }

    function LogLocation {
        connect(context.start, context.done);
        bind(name, context.result);
    }
}
