// Scenario 3: Complex task graph with multiple stages and functions

import "ue_core.d.gs";
import "task_nodes.d.gs";

Graph MultiStageQuest : TaskGraph {
    in questTitle : FString;
    in rewardAmount : int;
    out questComplete : bool;
    var currentStage : int;

    TaskStart beginning{};
    ShowDialogue intro{};
    ShowDialogue midpoint{};
    ShowDialogue conclusion{};
    TaskComplete ending{};

    event OnBegin {
        beginning.begin(intro.enter);
        intro.exit(midpoint.enter);
        link intro.text = questTitle;
        link intro.speaker = narrator;
    }

    function AdvanceStage {
        context.start(midpoint.enter);
        midpoint.exit(conclusion.enter);
        link midpoint.text = questTitle;
        link midpoint.speaker = narrator;
    }

    function CompleteQuest {
        context.start(conclusion.enter);
        conclusion.exit(ending.finish);
        link conclusion.text = questTitle;
        link ending.result = questComplete;
    }

    generate {
        Comment desc = "Multi-stage quest with branching dialogue";
        position:beginning.x(50);
        position:beginning.y(100);
        position:intro.x(200);
        position:intro.y(100);
        position:midpoint.x(350);
        position:midpoint.y(100);
        position:conclusion.x(500);
        position:conclusion.y(100);
        position:ending.x(650);
        position:ending.y(100);
    }
}
