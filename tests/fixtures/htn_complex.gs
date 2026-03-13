// Scenario 2: Complex HTN with branching exec fan-out (unlimited)
// Tests HTN schema: unlimited fan-out, no fan-in, strict type match

import "ue_core.d.gs";
import "htn_nodes.d.gs";

Graph PatrolAndEngage : HTNGraph {
    in patrolTarget : AActor;
    in engageTarget : AActor;
    in patrolSpeed : float;
    in engageSpeed : float;
    out success : bool;

    HTN_CheckDistance rangeCheck{};
    HTN_MoveToTarget movePatrol{};
    HTN_MoveToTarget moveEngage{};
    HTN_CheckDistance finalCheck{};
    PrintString logMessage{};

    event OnPlan {
        context.start(rangeCheck.enter);
        // Branching: inRange goes to engage, outOfRange goes to patrol
        rangeCheck.inRange(moveEngage.enter);
        rangeCheck.outOfRange(movePatrol.enter);
        // Both paths converge at finalCheck (NOT allowed in HTN - no fan-in)
        // We connect engage success to log instead
        moveEngage.success(logMessage.enter);
        movePatrol.success(finalCheck.enter);
        link rangeCheck.target = patrolTarget;
        link rangeCheck.threshold = patrolSpeed;
        link movePatrol.target = patrolTarget;
        link movePatrol.speed = patrolSpeed;
        link moveEngage.target = engageTarget;
        link moveEngage.speed = engageSpeed;
        link finalCheck.target = engageTarget;
        link finalCheck.threshold = engageSpeed;
    }
}
