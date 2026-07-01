import "ue_core.d.gs";
import "htn_nodes.d.gs";

graph SimpleHTN {

    schema HTNGraph;
    @graph.input
    param target: AActor;
    @graph.input
    param speed_default: float;
    node check {
        type HTN_CheckDistance;
    }
    node move {
        type HTN_MoveToTarget;
    }
    event OnPlan {
        connect(context.start, check.enter);
        connect(check.inRange, move.enter);
        bind(target, check.target);
        bind(target, move.target);
        bind(speed_default, move.speed);
    }
}
