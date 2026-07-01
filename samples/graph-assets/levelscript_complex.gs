// Scenario 4: Complex level script with multiple triggers, spawners, and data flow

import "ue_core.d.gs";
import "levelscript_nodes.d.gs";

@Comment("layout", "Arena encounter with 3 spawn waves")
graph ArenaEncounter {
    schema LevelScriptGraph;
    @graph.input
    param triggerVolume: AActor;
    @graph.input
    param bossClass: FName;
    @graph.input
    param spawn_loc_a: FVector;
    @graph.input
    param spawn_loc_b: FVector;
    @graph.input
    param spawn_loc_c: FVector;
    @graph.output
    param arenaCleared: bool;
    @graph.var
    param waveCount: int;
    @Position(X = 0, Y = 0)
    node mainTrigger {
        type TriggerVolume;
    }
    @Position(X = 200, Y = 0)
    node spawner1 {
        type SpawnActor;
    }
    @Position(X = 400, Y = 0)
    node spawner2 {
        type SpawnActor;
    }
    @Position(X = 200, Y = 200)
    node spawner3 {
        type SpawnActor;
    }
    @Position(X = 600, Y = 0)
    node waveAnnounce {
        type PrintString;
    }
    @Position(X = 400, Y = 200)
    node bossAnnounce {
        type PrintString;
    }
    event OnLevelStart {
        connect(mainTrigger.onEnter, spawner1.enter);
        connect(spawner1.exit, spawner2.enter);
        connect(spawner2.exit, waveAnnounce.enter);
        bind(triggerVolume, mainTrigger.volume);
        bind(bossClass, spawner1.actorClass);
        bind(spawn_loc_a, spawner1.location);
        bind(bossClass, spawner2.actorClass);
        bind(spawn_loc_b, spawner2.location);
        bind(waveCount, waveAnnounce.message);
    }

    event OnWaveComplete {
        connect(context.start, spawner3.enter);
        connect(spawner3.exit, bossAnnounce.enter);
        bind(bossClass, spawner3.actorClass);
        bind(spawn_loc_c, spawner3.location);
        bind(bossClass, bossAnnounce.message);
    }
}
