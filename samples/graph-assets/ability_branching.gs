// Scenario 10: Ability graph with unlimited fan-out, no fan-in

import "ue_core.d.gs";
import "mixed_declarations.d.gs";

graph FireballAbility {

    schema AbilityGraph;
    @graph.input
    param caster: AActor;
    @graph.input
    param target: AActor;
    @graph.input
    param damage: float;
    node tagCheck {
        type BranchOnTag;
    }
    node castAnim {
        type PlayMontage;
    }
    node fireball {
        type SpawnParticle;
    }
    node castSound {
        type PlaySound;
    }
    node dmgLog {
        type PrintString;
    }
    event OnActivate {
        connect(context.start, tagCheck.enter);
        // Unlimited fan-out: both matched and notMatched branch further
        connect(tagCheck.matched, castAnim.play);
        connect(tagCheck.matched, castSound.play);
        connect(tagCheck.matched, fireball.spawn);
        connect(tagCheck.notMatched, dmgLog.enter);
        connect(castAnim.completed, dmgLog.enter);
        bind(caster, tagCheck.target);
        bind(caster, castAnim.target);
        bind(damage, dmgLog.message);
    }
}
