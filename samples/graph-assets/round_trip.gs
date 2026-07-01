import "ue_core.d.gs";

const actor_1 = new SoftObjectPath {
    path: "actor_path_1";
};
@Comment("note1", "This is a test graph")
graph RoundTripTest {
    @graph.input
    param hp: int;
    @graph.output
    param damage: int;
    @graph.var
    param temp: float;
    @Position(X = 100, Y = 200)
    node printer {
        type PrintString;
    }
    @Position(X = 300, Y = 200)
    node delayer {
        type Delay;
    }
    event OnStart {
        connect(context.start, printer.enter);
        connect(printer.exit, delayer.enter);
        bind(hp, printer.message);
        bind(temp, delayer.duration);
    }

    function Calculate {
        connect(context.start, context.done);
        bind(damage, context.result);
    }
}
