import "ue_core.d.gs";

Graph SubRoutine {
    in value : int;
    out result : int;

    PrintString printer{};

    event Execute {
        context.start(printer.enter);
        printer.message = value;
    }
}

Graph MainGraph {
    in inputVal : int;

    SubRoutine sub{};
    PrintString final_printer{};

    event OnStart {
        context.start(sub.Execute);
        sub.value = inputVal;
    }
}
