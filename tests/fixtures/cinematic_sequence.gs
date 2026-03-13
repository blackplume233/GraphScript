// Scenario 9: Cinematic graph using CinematicGraph schema (strict, fan-out 1)

import "ue_core.d.gs";
import "mixed_declarations.d.gs";

Graph CutsceneIntro : CinematicGraph {
    in actor : AActor;
    in bgm : USoundBase;
    in vfx : UParticleSystem;
    out finished : bool;

    PlayMontage bow{};
    PlaySound music{};
    SpawnParticle sparkles{};
    PrintString subtitle{};

    event OnPlay {
        context.start(music.play);
        music.finished(bow.play);
        bow.completed(sparkles.spawn);
        sparkles.done(subtitle.enter);
        link music.sound = bgm;
        link music.volume = defaultVol;
        link bow.target = actor;
        link bow.montage = bowAnim;
        link bow.playRate = playSpeed;
        link sparkles.particle = vfx;
        link subtitle.message = introText;
    }
}
