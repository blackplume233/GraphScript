import "ue_core.d.gs";

graph BadGraph {
    node a {
        type PrintString;
    }
    node b {
        type PrintString;
    }
    event OnStart {
        connect(a.exit, b.enter);
        connect(a.exit, b.enter);
    }
}
