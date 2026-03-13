// Scenario 4: Complex level script with multiple triggers, spawners, and data flow

import "ue_core.d.gs";
import "levelscript_nodes.d.gs";

let spawn_loc_a = FVector("100,0,0");
let spawn_loc_b = FVector("200,0,0");
let spawn_loc_c = FVector("300,0,0");

Graph ArenaEncounter : LevelScriptGraph {
    in triggerVolume : AActor;
    in bossClass : FName;
    out arenaCleared : bool;
    var waveCount : int;

    TriggerVolume mainTrigger{};
    SpawnActor spawner1{};
    SpawnActor spawner2{};
    SpawnActor spawner3{};
    PrintString waveAnnounce{};
    PrintString bossAnnounce{};

    event OnLevelStart {
        mainTrigger.onEnter(spawner1.enter);
        spawner1.exit(spawner2.enter);
        spawner2.exit(waveAnnounce.enter);
        link mainTrigger.volume = triggerVolume;
        link spawner1.actorClass = bossClass;
        link spawner1.location = spawn_loc_a;
        link spawner2.actorClass = bossClass;
        link spawner2.location = spawn_loc_b;
        link waveAnnounce.message = waveCount;
    }

    event OnWaveComplete {
        context.start(spawner3.enter);
        spawner3.exit(bossAnnounce.enter);
        link spawner3.actorClass = bossClass;
        link spawner3.location = spawn_loc_c;
        link bossAnnounce.message = bossClass;
    }

    generate {
        Comment layout = "Arena encounter with 3 spawn waves";
        position:mainTrigger.x(0);
        position:mainTrigger.y(0);
        position:spawner1.x(200);
        position:spawner1.y(0);
        position:spawner2.x(400);
        position:spawner2.y(0);
        position:spawner3.x(200);
        position:spawner3.y(200);
        position:waveAnnounce.x(600);
        position:waveAnnounce.y(0);
        position:bossAnnounce.x(400);
        position:bossAnnounce.y(200);
    }
}
