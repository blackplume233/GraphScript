// Scenario 3: Complex task graph with multiple stages and functions

import "ue_core.d.gs";
import "task_nodes.d.gs";

@Comment("desc", "Multi-stage quest with branching dialogue")
graph MultiStageQuest {
    schema TaskGraph;
    @graph.input
    param questTitle: FString;
    @graph.input
    param rewardAmount: int;
    @graph.output
    param questComplete: bool;
    @graph.var
    param currentStage: int;
    @Position(X = 50, Y = 100)
    node beginning {
        type TaskStart;
    }
    @Position(X = 200, Y = 100)
    node intro {
        type ShowDialogue;
    }
    @Position(X = 350, Y = 100)
    node midpoint {
        type ShowDialogue;
    }
    @Position(X = 500, Y = 100)
    node conclusion {
        type ShowDialogue;
    }
    @Position(X = 650, Y = 100)
    node ending {
        type TaskComplete;
    }
    event OnBegin {
        connect(beginning.begin, intro.enter);
        connect(intro.exit, midpoint.enter);
        bind(questTitle, intro.text);
        bind(questTitle, intro.speaker);
    }

    function AdvanceStage {
        connect(context.start, context.done);
        bind(questTitle, context.result);
    }

    function CompleteQuest {
        connect(context.start, context.done);
        bind(questComplete, context.result);
    }
}
