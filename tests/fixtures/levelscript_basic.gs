import "ue_core.d.gs";
import "levelscript_nodes.d.gs";

let spawn_point = FVector("100,200,0");

Graph LevelSetup : LevelScriptGraph {
    in triggerActor : AActor;

    TriggerVolume trigger{};
    SpawnActor spawner{};
    PrintString printer{};

    event OnLevelStart {
        trigger.onEnter(spawner.enter);
        spawner.exit(printer.enter);
        link trigger.volume = triggerActor;
        link spawner.actorClass = enemyClass;
        link spawner.location = spawn_point;
        link printer.message = spawnMessage;
    }
}
