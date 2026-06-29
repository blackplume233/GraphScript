import "ue_core.d.gs";
import "levelscript_nodes.d.gs";

Graph LevelSetup : LevelScriptGraph {
    in triggerActor : AActor;
    in enemyClass : FName;
    in spawn_point : FVector;
    in spawnMessage : FString;

    TriggerVolume trigger{};
    SpawnActor spawner{};
    PrintString printer{};

    event OnLevelStart {
        trigger.onEnter(spawner.enter);
        spawner.exit(printer.enter);
        trigger.volume = triggerActor;
        spawner.actorClass = enemyClass;
        spawner.location = spawn_point;
        printer.message = spawnMessage;
    }
}
