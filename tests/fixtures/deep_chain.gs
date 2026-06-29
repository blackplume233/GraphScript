// Scenario 1: 3-level Graph-as-Node chain
// Leaf → Middle → Root, each graph references the previous as a node

import "ue_core.d.gs";

Graph Leaf {
    in seed : int;
    out value : int;

    PrintString debug{};

    event Compute {
        context.start(debug.enter);
        debug.message = seed;
    }
}

Graph Middle {
    in input : int;
    out output : int;

    Leaf leaf_a{};
    Leaf leaf_b{};

    event Process {
        context.start(leaf_a.Compute);
        leaf_a.seed = input;
        leaf_b.seed = input;
    }
}

Graph Root {
    in start_value : int;

    Middle mid{};
    PrintString final_print{};

    event OnStart {
        context.start(mid.Process);
        mid.input = start_value;
    }
}
