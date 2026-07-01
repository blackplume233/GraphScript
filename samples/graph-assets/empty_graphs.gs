// Scenario 6: Edge cases - empty graphs and minimal structures

import "ue_core.d.gs";

graph EmptyGraph {
}

graph ParamOnly {
    @graph.input
    param x: int;
    @graph.output
    param y: float;
    @graph.var
    param z: FString;
}

graph EventOnly {
    event Tick {
    }
    event OnDestroy {
    }
}

graph NodeOnly {
    node p {
        type PrintString;
    }
    node d {
        type Delay;
    }
}
