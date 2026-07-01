import "ue_core.d.gs";

graph HelloWorld {
    @graph.input
    param message: FString;
    node printer {
        type PrintString;
    }
    event OnStart {
        connect(context.start, printer.enter);
        bind(message, printer.message);
    }
}
