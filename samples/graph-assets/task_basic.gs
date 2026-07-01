import "ue_core.d.gs";
import "task_nodes.d.gs";

graph SimpleTask {

    schema TaskGraph;
    @graph.input
    param questName: FString;
    @graph.output
    param completed: bool;
    node start {
        type TaskStart;
    }
    node dialogue {
        type ShowDialogue;
    }
    node finish {
        type TaskComplete;
    }
    event OnBegin {
        connect(start.begin, dialogue.enter);
        connect(dialogue.exit, finish.finish);
        bind(questName, dialogue.text);
        bind(completed, finish.result);
    }
}
