import "ue_core.d.gs";

Graph BadGraph {
    PrintString a{};
    PrintString b{};

    event OnStart {
        a.exit(b.enter);
        a.exit(b.enter);
    }
}
