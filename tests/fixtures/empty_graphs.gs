// Scenario 6: Edge cases - empty graphs and minimal structures

import "ue_core.d.gs";

Graph EmptyGraph {
}

Graph ParamOnly {
    in x : int;
    out y : float;
    var z : FString;
}

Graph EventOnly {
    event Tick {
    }
    event OnDestroy {
    }
}

Graph NodeOnly {
    PrintString p{};
    Delay d{};
}
