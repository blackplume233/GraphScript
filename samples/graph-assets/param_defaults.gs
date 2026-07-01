// Scenario 11: Parameters with default values

import "ue_core.d.gs";

graph ConfigurableNode {
    @graph.input
    param speed: float = 1.0;
    @graph.input
    param health: int = 100;
    @graph.input
    param name: FString;
    @graph.output
    param result: bool;
    node p {
        type PrintString;
    }
    event OnRun {
        connect(context.start, p.enter);
        bind(name, p.message);
    }
}

graph UseConfigurable {
    @graph.input
    param label: FString;
    node worker {
        type ConfigurableNode;
    }
    event OnStart {
        connect(context.start, worker.OnRun);
        bind(label, worker.name);
    }
}
