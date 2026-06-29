import "ue_core.d.gs";
import "htn_nodes.d.gs";

Graph SimpleHTN : HTNGraph {
    in target : AActor;
    in speed_default : float;

    HTN_CheckDistance check{};
    HTN_MoveToTarget move{};

    event OnPlan {
        context.start(check.enter);
        check.inRange(move.enter);
        check.target = target;
        move.target = target;
        move.speed = speed_default;
    }
}
