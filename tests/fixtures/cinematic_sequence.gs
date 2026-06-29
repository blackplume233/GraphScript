// Scenario 9: Cinematic graph using CinematicGraph schema (strict, fan-out 1)

import "ue_core.d.gs";
import "mixed_declarations.d.gs";

Graph CutsceneIntro : CinematicGraph {
    in actor : AActor;
    in bgm : USoundBase;
    in vfx : UParticleSystem;
    in defaultVol : float;
    in bowAnim : UAnimMontage;
    in playSpeed : float;
    in introText : FString;
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
        music.sound = bgm;
        music.volume = defaultVol;
        bow.target = actor;
        bow.montage = bowAnim;
        bow.playRate = playSpeed;
        sparkles.particle = vfx;
        subtitle.message = introText;
    }
}
