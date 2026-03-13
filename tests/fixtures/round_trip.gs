import "ue_core.d.gs";

let actor_1 = SoftObjectPath("actor_path_1");

Graph RoundTripTest {
    in hp : int;
    out damage : int;
    var temp : float;

    PrintString printer{};
    Delay delayer{};

    event OnStart {
        context.start(printer.enter);
        printer.exit(delayer.enter);
        link printer.message = hp;
        link delayer.duration = temp;
    }

    function Calculate {
        context.start(printer.enter);
        link printer.message = damage;
    }

    generate {
        Comment note1 = "This is a test graph";
        position:printer.x(100);
        position:printer.y(200);
        position:delayer.x(300);
        position:delayer.y(200);
    }
}
