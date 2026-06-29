import "ue_core.d.gs";
import "task_nodes.d.gs";

Graph SimpleTask : TaskGraph {
    in questName : FString;
    out completed : bool;

    TaskStart start{};
    ShowDialogue dialogue{};
    TaskComplete finish{};

    event OnBegin {
        start.begin(dialogue.enter);
        dialogue.exit(finish.finish);
        dialogue.text = questName;
        finish.result = completed;
    }
}
