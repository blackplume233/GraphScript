import "ue_core.d.gs";

Graph SubRoutine {
    in value : int;
    out result : int;

    PrintString printer{};

    event Execute {
        context.start(printer.enter);
        link printer.message = value;
    }
}

Graph MainGraph {
    SubRoutine sub{};
    PrintString final_printer{};

    event OnStart {
        context.start(sub.Execute);
        link sub.value = inputVal;
    }
}
