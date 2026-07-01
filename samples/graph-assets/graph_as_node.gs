import "ue_core.d.gs";

graph SubRoutine {
    @graph.input
    param value: int;
    @graph.output
    param result: int;
    node printer {
        type PrintString;
    }
    event Execute {
        connect(context.start, printer.enter);
        bind(value, printer.message);
    }
}

graph MainGraph {
    @graph.input
    param inputVal: int;
    node sub {
        type SubRoutine;
    }
    node final_printer {
        type PrintString;
    }
    event OnStart {
        connect(context.start, sub.Execute);
        bind(inputVal, sub.value);
    }
}
