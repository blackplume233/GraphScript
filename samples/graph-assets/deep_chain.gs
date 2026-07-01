// Scenario 1: 3-level Graph-as-Node chain
// Leaf → Middle → Root, each graph references the previous as a node

import "ue_core.d.gs";

graph Leaf {
    @graph.input
    param seed: int;
    @graph.output
    param value: int;
    node debug {
        type PrintString;
    }
    event Compute {
        connect(context.start, debug.enter);
        bind(seed, debug.message);
    }
}

graph Middle {
    @graph.input
    param input: int;
    @graph.output
    param output: int;
    node leaf_a {
        type Leaf;
    }
    node leaf_b {
        type Leaf;
    }
    event Process {
        connect(context.start, leaf_a.Compute);
        bind(input, leaf_a.seed);
        bind(input, leaf_b.seed);
    }
}

graph Root {
    @graph.input
    param start_value: int;
    node mid {
        type Middle;
    }
    node final_print {
        type PrintString;
    }
    event OnStart {
        connect(context.start, mid.Process);
        bind(start_value, mid.input);
    }
}
