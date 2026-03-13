// Scenario 11: Parameters with default values

import "ue_core.d.gs";

Graph ConfigurableNode {
    in speed : float = 1.0;
    in health : int = 100;
    in name : FString;
    out result : bool;

    PrintString p{};

    event OnRun {
        context.start(p.enter);
        link p.message = name;
    }
}

Graph UseConfigurable {
    ConfigurableNode worker{};

    event OnStart {
        context.start(worker.OnRun);
        link worker.name = label;
    }
}
