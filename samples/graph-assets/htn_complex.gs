// Scenario 2: Complex HTN with branching exec fan-out (unlimited)
// Tests HTN schema: unlimited fan-out, no fan-in, strict type match

import "ue_core.d.gs";
import "htn_nodes.d.gs";

graph PatrolAndEngage {

    schema HTNGraph;
    @graph.input
    param patrolTarget: AActor;
    @graph.input
    param engageTarget: AActor;
    @graph.input
    param patrolSpeed: float;
    @graph.input
    param engageSpeed: float;
    @graph.output
    param success: bool;
    node rangeCheck {
        type HTN_CheckDistance;
    }
    node movePatrol {
        type HTN_MoveToTarget;
    }
    node moveEngage {
        type HTN_MoveToTarget;
    }
    node finalCheck {
        type HTN_CheckDistance;
    }
    node logMessage {
        type PrintString;
    }
    event OnPlan {
        connect(context.start, rangeCheck.enter);
        // Branching: inRange goes to engage, outOfRange goes to patrol
        connect(rangeCheck.inRange, moveEngage.enter);
        connect(rangeCheck.outOfRange, movePatrol.enter);
        // Both paths converge at finalCheck (NOT allowed in HTN - no fan-in)
        // We connect engage success to log instead
        connect(moveEngage.success, logMessage.enter);
        connect(movePatrol.success, finalCheck.enter);
        bind(patrolTarget, rangeCheck.target);
        bind(patrolSpeed, rangeCheck.threshold);
        bind(patrolTarget, movePatrol.target);
        bind(patrolSpeed, movePatrol.speed);
        bind(engageTarget, moveEngage.target);
        bind(engageSpeed, moveEngage.speed);
        bind(engageTarget, finalCheck.target);
        bind(engageSpeed, finalCheck.threshold);
    }
}
