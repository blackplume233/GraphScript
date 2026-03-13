// Scenario 3: Complex task graph with multiple stages and functions

import "ue_core.d.gs";
import "task_nodes.d.gs";

[Comment("desc", "Multi-stage quest with branching dialogue")]
Graph MultiStageQuest : TaskGraph {
    in questTitle : FString;
    in rewardAmount : int;
    out questComplete : bool;
    var currentStage : int;

    [Position(X = 50, Y = 100)]
    TaskStart beginning{};
    [Position(X = 200, Y = 100)]
    ShowDialogue intro{};
    [Position(X = 350, Y = 100)]
    ShowDialogue midpoint{};
    [Position(X = 500, Y = 100)]
    ShowDialogue conclusion{};
    [Position(X = 650, Y = 100)]
    TaskComplete ending{};

    event OnBegin {
        beginning.begin(intro.enter);
        intro.exit(midpoint.enter);
        link intro.text = questTitle;
        link intro.speaker = questTitle;
    }

    function AdvanceStage {
        context.start(context.done);
        link context.result = questTitle;
    }

    function CompleteQuest {
        context.start(context.done);
        link context.result = questComplete;
    }
}
