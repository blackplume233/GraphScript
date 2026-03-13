import "ue_core.d.gs";

Graph HelloWorld {
    in message : FString;

    PrintString printer{};

    event OnStart {
        context.start(printer.enter);
        link printer.message = message;
    }
}
