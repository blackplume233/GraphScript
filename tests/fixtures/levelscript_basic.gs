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
        link trigger.volume = triggerActor;
        link spawner.actorClass = enemyClass;
        link spawner.location = spawn_point;
        link printer.message = spawnMessage;
    }
}
