import "ue_core.d.gs";
import "levelscript_nodes.d.gs";

graph LevelSetup {

    schema LevelScriptGraph;
    @graph.input
    param triggerActor: AActor;
    @graph.input
    param enemyClass: FName;
    @graph.input
    param spawn_point: FVector;
    @graph.input
    param spawnMessage: FString;
    node trigger {
        type TriggerVolume;
    }
    node spawner {
        type SpawnActor;
    }
    node printer {
        type PrintString;
    }
    event OnLevelStart {
        connect(trigger.onEnter, spawner.enter);
        connect(spawner.exit, printer.enter);
        bind(triggerActor, trigger.volume);
        bind(enemyClass, spawner.actorClass);
        bind(spawn_point, spawner.location);
        bind(spawnMessage, printer.message);
    }
}
