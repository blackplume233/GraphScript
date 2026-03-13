import "ue_core.d.gs";

let actor_1 = SoftObjectPath("actor_path_1");

[Comment("note1", "This is a test graph")]
Graph RoundTripTest {
    in hp : int;
    out damage : int;
    var temp : float;

    [Position(X = 100, Y = 200)]
    PrintString printer{};
    [Position(X = 300, Y = 200)]
    Delay delayer{};

    event OnStart {
        context.start(printer.enter);
        printer.exit(delayer.enter);
        link printer.message = hp;
        link delayer.duration = temp;
    }

    function Calculate {
        context.start(context.done);
        link context.result = damage;
    }
}
