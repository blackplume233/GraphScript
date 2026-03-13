// Scenario 10: Ability graph with unlimited fan-out, no fan-in

import "ue_core.d.gs";
import "mixed_declarations.d.gs";

Graph FireballAbility : AbilityGraph {
    in caster : AActor;
    in target : AActor;
    in damage : float;

    BranchOnTag tagCheck{};
    PlayMontage castAnim{};
    SpawnParticle fireball{};
    PlaySound castSound{};
    PrintString dmgLog{};

    event OnActivate {
        context.start(tagCheck.enter);
        // Unlimited fan-out: both matched and notMatched branch further
        tagCheck.matched(castAnim.play);
        tagCheck.matched(castSound.play);
        tagCheck.matched(fireball.spawn);
        tagCheck.notMatched(dmgLog.enter);
        castAnim.completed(dmgLog.enter);
        link tagCheck.target = caster;
        link castAnim.target = caster;
        link dmgLog.message = damage;
    }
}
