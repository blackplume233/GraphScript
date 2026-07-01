// Scenario 9: Cinematic graph using CinematicGraph schema (strict, fan-out 1)

import "ue_core.d.gs";
import "mixed_declarations.d.gs";

graph CutsceneIntro {

    schema CinematicGraph;
    @graph.input
    param actor: AActor;
    @graph.input
    param bgm: USoundBase;
    @graph.input
    param vfx: UParticleSystem;
    @graph.input
    param defaultVol: float;
    @graph.input
    param bowAnim: UAnimMontage;
    @graph.input
    param playSpeed: float;
    @graph.input
    param introText: FString;
    @graph.output
    param finished: bool;
    node bow {
        type PlayMontage;
    }
    node music {
        type PlaySound;
    }
    node sparkles {
        type SpawnParticle;
    }
    node subtitle {
        type PrintString;
    }
    event OnPlay {
        connect(context.start, music.play);
        connect(music.finished, bow.play);
        connect(bow.completed, sparkles.spawn);
        connect(sparkles.done, subtitle.enter);
        bind(bgm, music.sound);
        bind(defaultVol, music.volume);
        bind(actor, bow.target);
        bind(bowAnim, bow.montage);
        bind(playSpeed, bow.playRate);
        bind(vfx, sparkles.particle);
        bind(introText, subtitle.message);
    }
}
