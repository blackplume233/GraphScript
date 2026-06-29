// Scenario 4: Complex level script with multiple triggers, spawners, and data flow

import "ue_core.d.gs";
import "levelscript_nodes.d.gs";

[Comment("layout", "Arena encounter with 3 spawn waves")]
Graph ArenaEncounter : LevelScriptGraph {
    in triggerVolume : AActor;
    in bossClass : FName;
    in spawn_loc_a : FVector;
    in spawn_loc_b : FVector;
    in spawn_loc_c : FVector;
    out arenaCleared : bool;
    var waveCount : int;

    [Position(X = 0, Y = 0)]
    TriggerVolume mainTrigger{};
    [Position(X = 200, Y = 0)]
    SpawnActor spawner1{};
    [Position(X = 400, Y = 0)]
    SpawnActor spawner2{};
    [Position(X = 200, Y = 200)]
    SpawnActor spawner3{};
    [Position(X = 600, Y = 0)]
    PrintString waveAnnounce{};
    [Position(X = 400, Y = 200)]
    PrintString bossAnnounce{};

    event OnLevelStart {
        mainTrigger.onEnter(spawner1.enter);
        spawner1.exit(spawner2.enter);
        spawner2.exit(waveAnnounce.enter);
        mainTrigger.volume = triggerVolume;
        spawner1.actorClass = bossClass;
        spawner1.location = spawn_loc_a;
        spawner2.actorClass = bossClass;
        spawner2.location = spawn_loc_b;
        waveAnnounce.message = waveCount;
    }

    event OnWaveComplete {
        context.start(spawner3.enter);
        spawner3.exit(bossAnnounce.enter);
        spawner3.actorClass = bossClass;
        spawner3.location = spawn_loc_c;
        bossAnnounce.message = bossClass;
    }
}
