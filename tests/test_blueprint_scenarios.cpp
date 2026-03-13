/// Blueprint Scenario Tests: realistic UE Blueprint graphs implemented in GraphScript.
/// Each scenario models a real game feature using the extended node library.

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>

#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"
#include "graphscript/compile/compiler.h"
#include "graphscript/edit/edit_session.h"
#include "graphscript/edit/edit_graph.h"
#include "graphscript/runtime/runtime_graph.h"
#include "graphscript/emit/emitter.h"
#include "graphscript/registry/environment.h"
#include "graphscript/debug/dump.h"

using namespace gs;

// ─── Helpers ───────────────────────────────────────────────────────

static std::string read_fixture(const std::string& filename) {
    std::string path = std::string(GS_TEST_FIXTURES_DIR) + "/" + filename;
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::unique_ptr<ModuleNode> do_parse(const std::string& src) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto result = parser.parse();
    if (result.is_err()) return nullptr;
    return std::move(result).value();
}

static Module do_compile(const std::string& src, Environment& env, const std::string& path = "") {
    auto ast = do_parse(src);
    if (!ast) return {};
    Compiler compiler(env);
    auto result = compiler.compile(*ast, path);
    if (result.is_err()) return {};
    return std::move(result).value();
}

static void load_all(Environment& env) {
    do_compile(read_fixture("ue_core.d.gs"), env, "ue_core.d.gs");
    do_compile(read_fixture("ue_blueprint.d.gs"), env, "ue_blueprint.d.gs");
}

static void assert_modules_eq(const Module& a, const Module& b,
                               const char* file, int line) {
    auto diff = debug::diff_modules(a, b);
    if (!diff.equal) {
        std::string summary = std::string(file) + ":" + std::to_string(line) + " — ";
        summary += std::to_string(diff.differences.size()) + " diffs. First 5:\n";
        for (size_t i = 0; i < std::min(diff.differences.size(), size_t(5)); ++i)
            summary += "  " + diff.differences[i] + "\n";
        FAIL() << summary;
    }
}
#define ASSERT_MODULES_EQ(a, b) assert_modules_eq(a, b, __FILE__, __LINE__)

static void assert_round_trip(const Module& mod, Environment& env,
                               const char* file, int line) {
    Emitter emitter;
    std::string text = emitter.emit(mod);
    if (text.empty()) { ADD_FAILURE_AT(file, line) << "Empty emit"; return; }
    auto ast = do_parse(text);
    if (!ast) { ADD_FAILURE_AT(file, line) << "Re-parse failed:\n" << text; return; }
    Compiler c(env);
    auto r = c.compile(*ast);
    if (r.is_err()) { ADD_FAILURE_AT(file, line) << "Re-compile: " << r.error(); return; }
    assert_modules_eq(mod, r.value(), file, line);
}
#define ASSERT_ROUND_TRIP(mod, env) assert_round_trip(mod, env, __FILE__, __LINE__)

// ═══════════════════════════════════════════════════════════════════
// BP1: Health System — take damage, clamp HP, check death, update UI
//
// Blueprint equivalent:
//   OnTakeDamage → Subtract HP → Clamp(0,MaxHP) → Branch(HP<=0)
//     True  → PlayDeathMontage → DestroyActor
//     False → UpdateHPBar → PrintDebugHP
// ═══════════════════════════════════════════════════════════════════

TEST(Blueprint, BP1_HealthSystem) {
    const char* src = R"(
import "ue_core.d.gs";
import "ue_blueprint.d.gs";

[Comment("title", "Character Health System")]
Graph HealthSystem {
    in maxHP : float;
    in currentHP : float;
    in incomingDamage : float;
    in self : ACharacter;
    in hpBarWidget : UUserWidget;
    in deathMontage : UAnimMontage;
    out isDead : bool;

    Subtract_Float calcNewHP{};
    Clamp_Float clampHP{};
    Compare_Float checkDeath{};
    Branch deathBranch{};

    [Position(X = 600, Y = 200)]
    PlayAnimMontage playDeath{};
    Delay deathDelay{};
    DestroyActor cleanup{};

    SetProgressBar updateBar{};
    Divide_Float hpPercent{};
    FloatToString hpText{};
    PrintString debugHP{};

    event OnTakeDamage {
        context.start(deathBranch.enter);

        link calcNewHP.A = currentHP;
        link calcNewHP.B = incomingDamage;

        link clampHP.Value = calcNewHP.Result;
        link clampHP.Min = isDead;
        link clampHP.Max = maxHP;

        link checkDeath.A = clampHP.Result;
        link checkDeath.B = isDead;

        link deathBranch.condition = checkDeath.IsEqual;

        deathBranch.onTrue(playDeath.enter);
        playDeath.exit(deathDelay.enter);
        deathDelay.completed(cleanup.enter);
        link playDeath.target = self;
        link playDeath.montage = deathMontage;
        link deathDelay.duration = maxHP;
        link cleanup.target = self;

        deathBranch.onFalse(updateBar.enter);
        updateBar.exit(debugHP.enter);

        link hpPercent.A = clampHP.Result;
        link hpPercent.B = maxHP;
        link updateBar.widget = hpBarWidget;
        link updateBar.percent = hpPercent.Result;

        link hpText.value = clampHP.Result;
        link debugHP.message = hpText.result;
    }
}
)";

    Environment env;
    load_all(env);
    auto mod = do_compile(src, env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.name, "HealthSystem");
    EXPECT_EQ(g.node_instances.size(), 11u);
    EXPECT_EQ(g.parameters.size(), 7u);
    EXPECT_EQ(g.events.size(), 1u);
    EXPECT_EQ(g.events[0].flow_connections.size(), 6u);
    EXPECT_GT(g.events[0].data_links.size(), 10u);

    Environment env2;
    load_all(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    auto eg = EditGraph::build(g, env);
    EXPECT_EQ(eg.node_count(), 11u);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 11u);
}

// ═══════════════════════════════════════════════════════════════════
// BP2: Item Pickup — overlap detection, validity check, add to inventory
//
// Blueprint:
//   OnOverlap → IsValid(OtherActor) → Branch
//     True  → GetPlayerCharacter → GetDistance → Branch(< threshold)
//       True → PlaySound → SpawnParticle → DestroyPickup
//     False → (nothing)
// ═══════════════════════════════════════════════════════════════════

TEST(Blueprint, BP2_ItemPickup) {
    const char* src = R"(
import "ue_core.d.gs";
import "ue_blueprint.d.gs";

[Comment("title", "Pickup Item Blueprint")]
Graph PickupItem {
    in self : AActor;
    in otherActor : AActor;
    in pickupRadius : float;
    in pickupSound : USoundBase;
    in pickupParticle : UParticleSystem;
    out wasPickedUp : bool;

    IsValid checkValid{};
    Branch validBranch{};
    GetDistanceTo distCheck{};
    Compare_Float rangeCheck{};
    Branch rangeBranch{};
    GetActorLocation selfLoc{};

    PlaySoundAtLocation playSound{};
    SpawnEmitterAtLocation spawnFX{};
    DestroyActor destroySelf{};
    PrintString debugMsg{};

    event OnOverlapBegin {
        context.start(validBranch.enter);
        link checkValid.object = otherActor;
        link validBranch.condition = checkValid.valid;

        validBranch.onTrue(rangeBranch.enter);
        link distCheck.from = self;
        link distCheck.to = otherActor;
        link rangeCheck.A = distCheck.distance;
        link rangeCheck.B = pickupRadius;
        link rangeBranch.condition = rangeCheck.IsLess;

        rangeBranch.onTrue(playSound.enter);
        link selfLoc.target = self;
        link playSound.sound = pickupSound;
        link playSound.location = selfLoc.location;

        playSound.exit(spawnFX.enter);
        link spawnFX.particle = pickupParticle;
        link spawnFX.location = selfLoc.location;

        spawnFX.exit(debugMsg.enter);
        debugMsg.exit(destroySelf.enter);
        link destroySelf.target = self;
    }
}
)";

    Environment env;
    load_all(env);
    auto mod = do_compile(src, env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.node_instances.size(), 10u);
    EXPECT_EQ(g.events[0].flow_connections.size(), 6u);

    Environment env2;
    load_all(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    auto eg = EditGraph::build(g, env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 10u);
}

// ═══════════════════════════════════════════════════════════════════
// BP3: AI Patrol — move between waypoints, wait, check player distance
//
// Blueprint:
//   OnPatrolTick → GetDistance(Self, Player) → Branch(< alertRange)
//     True  → Enter combat (print alert)
//     False → MoveToCurrentWaypoint → Delay → AdvanceIndex → Loop
// ═══════════════════════════════════════════════════════════════════

TEST(Blueprint, BP3_AIPatrol) {
    const char* src = R"(
import "ue_core.d.gs";
import "ue_blueprint.d.gs";

[Comment("title", "AI Patrol Behavior")]
Graph AIPatrol {
    in self : ACharacter;
    in alertRange : float;
    in patrolSpeed : float;
    in waypointA : FVector;
    in waypointB : FVector;
    in waitTime : float;
    out isAlerted : bool;

    GetPlayerCharacter getPlayer{};
    GetDistanceTo distToPlayer{};
    Compare_Float alertCheck{};
    Branch alertBranch{};

    PrintString alertLog{};

    [Position(X = 400, Y = 300)]
    AddMovementInput moveToWP{};
    Delay waitAtWP{};
    FlipFlop wpToggle{};
    Select_Float pickSpeed{};
    GetActorForwardVector getForward{};

    PrintString patrolLog{};
    FloatToString distText{};
    AppendStrings logMsg{};

    event OnPatrolTick {
        context.start(alertBranch.enter);

        link distToPlayer.from = self;
        link distToPlayer.to = getPlayer.character;
        link alertCheck.A = distToPlayer.distance;
        link alertCheck.B = alertRange;
        link alertBranch.condition = alertCheck.IsLess;

        alertBranch.onTrue(alertLog.enter);
        link alertLog.message = isAlerted;

        alertBranch.onFalse(wpToggle.enter);
        wpToggle.outA(moveToWP.enter);
        wpToggle.outB(moveToWP.enter);

        moveToWP.exit(waitAtWP.enter);
        link moveToWP.target = self;
        link moveToWP.direction = getForward.forward;
        link moveToWP.scale = patrolSpeed;
        link getForward.target = self;
        link waitAtWP.duration = waitTime;

        waitAtWP.completed(patrolLog.enter);
        link distText.value = distToPlayer.distance;
        link logMsg.A = isAlerted;
        link logMsg.B = distText.result;
        link patrolLog.message = logMsg.Result;
    }
}
)";

    Environment env;
    load_all(env);
    auto mod = do_compile(src, env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.node_instances.size(), 13u);
    EXPECT_EQ(g.parameters.size(), 7u);

    Environment env2;
    load_all(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    auto eg = EditGraph::build(g, env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 13u);
}

// ═══════════════════════════════════════════════════════════════════
// BP4: HUD Manager — create widgets, bind health bar, show/hide
//
// Blueprint:
//   OnBeginPlay → CreateWidget(HUD) → AddToViewport
//   OnHealthChanged → SetProgressBar(hp%) → SetText(hp string)
//   OnToggleHUD → Branch(visible) → SetVisibility
// ═══════════════════════════════════════════════════════════════════

TEST(Blueprint, BP4_HUDManager) {
    const char* src = R"(
import "ue_core.d.gs";
import "ue_blueprint.d.gs";

[Comment("title", "HUD Manager")]
Graph HUDManager {
    in playerController : APlayerController;
    in hudClass : UClass;
    in maxHP : float;
    in currentHP : float;
    in hudVisible : bool;
    var hudWidget : UUserWidget;

    CreateWidget createHud{};
    AddToViewport showHud{};

    Divide_Float calcPercent{};
    SetProgressBar setHPBar{};
    FloatToString hpStr{};
    AppendStrings makeLabel{};
    SetTextBlock setHPText{};
    PrintString debugHud{};

    Branch visBranch{};
    SetVisibility setVis{};

    event OnBeginPlay {
        context.start(createHud.enter);
        createHud.exit(showHud.enter);
        link createHud.widgetClass = hudClass;
        link createHud.owner = playerController;
        link showHud.widget = createHud.widget;
    }

    event OnHealthChanged {
        context.start(setHPBar.enter);
        setHPBar.exit(setHPText.enter);
        setHPText.exit(debugHud.enter);

        link calcPercent.A = currentHP;
        link calcPercent.B = maxHP;
        link setHPBar.widget = hudWidget;
        link setHPBar.percent = calcPercent.Result;

        link hpStr.value = currentHP;
        link makeLabel.A = hpStr.result;
        link makeLabel.B = maxHP;
        link setHPText.widget = hudWidget;
        link setHPText.text = makeLabel.Result;
    }

    event OnToggleHUD {
        context.start(visBranch.enter);
        visBranch.onTrue(setVis.enter);
        visBranch.onFalse(setVis.enter);
        link visBranch.condition = hudVisible;
        link setVis.widget = hudWidget;
        link setVis.visible = hudVisible;
    }
}
)";

    Environment env;
    load_all(env);
    auto mod = do_compile(src, env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.node_instances.size(), 10u);
    EXPECT_EQ(g.events.size(), 3u);

    Environment env2;
    load_all(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    auto eg = EditGraph::build(g, env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 10u);
}

// ═══════════════════════════════════════════════════════════════════
// BP5: Projectile System — spawn, launch, trace, impact damage + FX
//
// Blueprint:
//   OnFire → SpawnProjectile → SetVelocity
//   OnProjectileTick → LineTrace → Branch(hit)
//     True  → ApplyDamage → SpawnImpactFX → SpawnSound → Destroy
//     False → continue
// ═══════════════════════════════════════════════════════════════════

TEST(Blueprint, BP5_ProjectileSystem) {
    const char* src = R"(
import "ue_core.d.gs";
import "ue_blueprint.d.gs";

[Comment("title", "Projectile Fire and Impact")]
Graph ProjectileSystem {
    in muzzleLocation : FVector;
    in fireDirection : FVector;
    in projectileClass : UClass;
    in projectileSpeed : float;
    in impactDamage : float;
    in instigator : APlayerController;
    in impactSound : USoundBase;
    in impactParticle : UParticleSystem;
    in traceLength : float;

    SpawnActorFromClass spawnProj{};
    Multiply_VectorFloat calcVelocity{};
    MakeVector muzzleOffset{};
    Add_Vector traceEnd{};
    PrintString fireLog{};

    LineTraceByChannel trace{};
    ApplyDamage dealDamage{};
    SpawnEmitterAtLocation impactFX{};
    PlaySoundAtLocation impactSnd{};
    DestroyActor destroyProj{};
    DrawDebugLine debugTrace{};

    FloatToString dmgStr{};
    AppendStrings hitMsg{};
    PrintString hitLog{};

    event OnFire {
        context.start(spawnProj.enter);
        spawnProj.exit(fireLog.enter);

        link spawnProj.actorClass = projectileClass;
        link spawnProj.location = muzzleLocation;

        link calcVelocity.Vec = fireDirection;
        link calcVelocity.Scale = projectileSpeed;

        link fireLog.message = instigator;
    }

    event OnProjectileTick {
        context.start(trace.enter);

        link traceEnd.A = muzzleLocation;
        link traceEnd.B = calcVelocity.Result;
        link trace.start = muzzleLocation;
        link trace.end = traceEnd.Result;

        trace.hit(dealDamage.enter);
        dealDamage.exit(impactFX.enter);
        impactFX.exit(impactSnd.enter);
        impactSnd.exit(hitLog.enter);
        hitLog.exit(destroyProj.enter);

        link dealDamage.target = spawnProj.spawnedActor;
        link dealDamage.damage = impactDamage;
        link dealDamage.instigator = instigator;

        link impactFX.particle = impactParticle;
        link impactFX.location = trace.hitLocation;

        link impactSnd.sound = impactSound;
        link impactSnd.location = trace.hitLocation;

        link dmgStr.value = impactDamage;
        link hitMsg.A = dmgStr.result;
        link hitMsg.B = instigator;
        link hitLog.message = hitMsg.Result;
        link destroyProj.target = spawnProj.spawnedActor;

        trace.noHit(debugTrace.enter);
        link debugTrace.start = muzzleLocation;
        link debugTrace.end = traceEnd.Result;
    }
}
)";

    Environment env;
    load_all(env);
    auto mod = do_compile(src, env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.node_instances.size(), 14u);
    EXPECT_EQ(g.events.size(), 2u);
    EXPECT_EQ(g.parameters.size(), 9u);

    Environment env2;
    load_all(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    auto eg = EditGraph::build(g, env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 14u);
}

// ═══════════════════════════════════════════════════════════════════
// BP6: Combo via editor — build a melee combo system through EditSession
//
// 3-hit combo: LightAttack → HeavyAttack → Finisher
// Each hit: PlayMontage → ApplyDamage → Check combo window
// ═══════════════════════════════════════════════════════════════════

TEST(Blueprint, BP6_MeleeCombo_EditorPath) {
    Environment env;
    load_all(env);
    EditSession session(env);
    session.add_import("ue_core.d.gs");
    session.add_import("ue_blueprint.d.gs");

    ASSERT_TRUE(session.new_graph("MeleeCombo").is_ok());
    auto* g = session.active_graph();
    g->annotations.push_back({"Comment", {{"", "title"}, {"", "Melee Combo System"}}});

    ASSERT_TRUE(session.add_param(ParamDirection::In, "self", "ACharacter").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "lightMontage", "UAnimMontage").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "heavyMontage", "UAnimMontage").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "finishMontage", "UAnimMontage").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "comboDamage", "float").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "comboTarget", "AActor").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "comboWindow", "float").is_ok());

    // Nodes for 3-hit combo
    ASSERT_TRUE(session.add_node("PlayAnimMontage", "playLight").is_ok());
    g->node_instances[0].annotations.push_back({"Position", {{"X", "100"}, {"Y", "100"}}});

    ASSERT_TRUE(session.add_node("ApplyDamage", "dmgLight").is_ok());
    ASSERT_TRUE(session.add_node("Delay", "comboWait1").is_ok());
    ASSERT_TRUE(session.add_node("Branch", "comboCheck1").is_ok());

    ASSERT_TRUE(session.add_node("PlayAnimMontage", "playHeavy").is_ok());
    g->node_instances[4].annotations.push_back({"Position", {{"X", "400"}, {"Y", "100"}}});

    ASSERT_TRUE(session.add_node("ApplyDamage", "dmgHeavy").is_ok());
    ASSERT_TRUE(session.add_node("Delay", "comboWait2").is_ok());
    ASSERT_TRUE(session.add_node("Branch", "comboCheck2").is_ok());

    ASSERT_TRUE(session.add_node("PlayAnimMontage", "playFinish").is_ok());
    g->node_instances[8].annotations.push_back({"Position", {{"X", "700"}, {"Y", "100"}}});

    ASSERT_TRUE(session.add_node("ApplyDamage", "dmgFinish").is_ok());
    ASSERT_TRUE(session.add_node("Multiply_Float", "finishMultiplier").is_ok());

    ASSERT_TRUE(session.add_node("PrintString", "comboLog").is_ok());
    ASSERT_TRUE(session.add_node("FloatToString", "dmgText").is_ok());

    // Event: OnLightAttack — start the combo chain
    ASSERT_TRUE(session.add_event("OnLightAttack").is_ok());
    ASSERT_TRUE(session.add_flow("OnLightAttack", "context", "start", "playLight", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnLightAttack", "playLight", "exit", "dmgLight", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnLightAttack", "dmgLight", "exit", "comboWait1", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnLightAttack", "comboWait1", "completed", "comboCheck1", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "playLight", "target", "self").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "playLight", "montage", "lightMontage").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "dmgLight", "target", "comboTarget").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "dmgLight", "damage", "comboDamage").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "comboWait1", "duration", "comboWindow").is_ok());

    // Combo check 1 → heavy attack or drop
    ASSERT_TRUE(session.add_flow("OnLightAttack", "comboCheck1", "onTrue", "playHeavy", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnLightAttack", "playHeavy", "exit", "dmgHeavy", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnLightAttack", "dmgHeavy", "exit", "comboWait2", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnLightAttack", "comboWait2", "completed", "comboCheck2", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "playHeavy", "target", "self").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "playHeavy", "montage", "heavyMontage").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "dmgHeavy", "target", "comboTarget").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "dmgHeavy", "damage", "comboDamage").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "comboWait2", "duration", "comboWindow").is_ok());

    // Combo check 2 → finisher or drop
    ASSERT_TRUE(session.add_flow("OnLightAttack", "comboCheck2", "onTrue", "playFinish", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnLightAttack", "playFinish", "exit", "dmgFinish", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnLightAttack", "dmgFinish", "exit", "comboLog", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "playFinish", "target", "self").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "playFinish", "montage", "finishMontage").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "finishMultiplier", "A", "comboDamage").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "dmgFinish", "target", "comboTarget").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "dmgFinish", "damage", "finishMultiplier", "Result").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "dmgText", "value", "comboDamage").is_ok());
    ASSERT_TRUE(session.add_link("OnLightAttack", "comboLog", "message", "dmgText", "result").is_ok());

    auto& mod = session.module();
    EXPECT_EQ(mod.graphs[0].node_instances.size(), 13u);
    EXPECT_EQ(mod.graphs[0].events[0].flow_connections.size(), 11u);
    EXPECT_GT(mod.graphs[0].events[0].data_links.size(), 15u);

    // Round-trip
    Environment env2;
    load_all(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    // Bake
    auto eg = EditGraph::build(mod.graphs[0], env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 13u);
}

// ═══════════════════════════════════════════════════════════════════
// BP7: Full game loop — combine multiple subgraphs into one module
//
// HealthSystem (graph-as-node) + HUD + Patrol all in one file,
// with a top-level GameController graph orchestrating them
// ═══════════════════════════════════════════════════════════════════

TEST(Blueprint, BP7_GameController_MultiGraph) {
    const char* src = R"(
import "ue_core.d.gs";
import "ue_blueprint.d.gs";

Graph DamageCalculator {
    in baseDamage : float;
    in multiplier : float;
    in armor : float;
    out finalDamage : float;

    Multiply_Float rawDmg{};
    Subtract_Float afterArmor{};
    Clamp_Float clampDmg{};

    event Calculate {
        context.start(context.done);
        link rawDmg.A = baseDamage;
        link rawDmg.B = multiplier;
        link afterArmor.A = rawDmg.Result;
        link afterArmor.B = armor;
        link clampDmg.Value = afterArmor.Result;
    }

    function GetResult {
        context.start(context.done);
        link context.result = finalDamage;
    }
}

Graph SpawnManager {
    in spawnPoint : FVector;
    in spawnClass : UClass;
    in spawnCount : int;
    out lastSpawned : AActor;

    ForLoop spawnLoop{};
    SpawnActorFromClass spawner{};
    RandomFloatInRange randOffset{};
    MakeVector offsetVec{};
    Add_Vector spawnLoc{};
    PrintString spawnLog{};
    IntToString indexStr{};

    event DoSpawn {
        context.start(spawnLoop.enter);
        link spawnLoop.lastIndex = spawnCount;

        spawnLoop.loopBody(spawner.enter);
        spawner.exit(spawnLog.enter);

        link randOffset.Min = spawnCount;
        link randOffset.Max = spawnCount;
        link offsetVec.X = randOffset.Result;
        link spawnLoc.A = spawnPoint;
        link spawnLoc.B = offsetVec.Result;
        link spawner.actorClass = spawnClass;
        link spawner.location = spawnLoc.Result;

        link indexStr.value = spawnLoop.index;
        link spawnLog.message = indexStr.result;
    }
}

Graph GameController {
    in player : ACharacter;
    in playerController : APlayerController;
    in hudClass : UClass;

    DamageCalculator dmgCalc{};
    SpawnManager spawner{};

    CreateWidget createHud{};
    AddToViewport showHud{};
    PrintString startLog{};
    SetTimer tickTimer{};
    GetActorLocation playerLoc{};

    event OnGameStart {
        context.start(createHud.enter);
        createHud.exit(showHud.enter);
        showHud.exit(startLog.enter);
        startLog.exit(tickTimer.enter);

        link createHud.widgetClass = hudClass;
        link createHud.owner = playerController;
        link showHud.widget = createHud.widget;
    }

    event OnGameTick {
        context.start(dmgCalc.Calculate);
        link playerLoc.target = player;
    }
}
)";

    Environment env;
    load_all(env);
    auto mod = do_compile(src, env);
    ASSERT_EQ(mod.graphs.size(), 3u);

    EXPECT_EQ(mod.graphs[0].name, "DamageCalculator");
    EXPECT_EQ(mod.graphs[0].node_instances.size(), 3u);
    EXPECT_EQ(mod.graphs[0].events.size(), 1u);
    EXPECT_EQ(mod.graphs[0].functions.size(), 1u);

    EXPECT_EQ(mod.graphs[1].name, "SpawnManager");
    EXPECT_EQ(mod.graphs[1].node_instances.size(), 7u);

    EXPECT_EQ(mod.graphs[2].name, "GameController");
    EXPECT_EQ(mod.graphs[2].node_instances.size(), 7u);
    // GameController uses DamageCalculator and SpawnManager as graph-as-node
    bool has_dmg = false, has_spawn = false;
    for (auto& ni : mod.graphs[2].node_instances) {
        if (ni.type_name == "DamageCalculator") has_dmg = true;
        if (ni.type_name == "SpawnManager") has_spawn = true;
    }
    EXPECT_TRUE(has_dmg);
    EXPECT_TRUE(has_spawn);

    // Round-trip all graphs
    Environment env2;
    load_all(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    // Bake each graph
    for (auto& g : mod.graphs) {
        auto eg = EditGraph::build(g, env);
        auto rg = RuntimeGraph::bake(eg);
        EXPECT_GT(rg.node_count(), 0u);
    }

    // Dump the full module for visibility
    std::string full_dump = debug::dump_module(mod);
    EXPECT_GT(full_dump.size(), 500u);
}

// ═══════════════════════════════════════════════════════════════════
// BP8: Editor build + text round-trip of all scenarios combined
// ═══════════════════════════════════════════════════════════════════

TEST(Blueprint, BP8_AllScenarios_StressRoundTrip) {
    Environment env;
    load_all(env);

    // Build a 5-graph module via editor covering all patterns
    EditSession session(env);
    session.add_import("ue_core.d.gs");
    session.add_import("ue_blueprint.d.gs");

    // Graph 1: Branch + Math
    ASSERT_TRUE(session.new_graph("MathBranch").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "a", "float").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "b", "float").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Out, "result", "float").is_ok());
    ASSERT_TRUE(session.add_node("Add_Float", "adder").is_ok());
    ASSERT_TRUE(session.add_node("Compare_Float", "cmp").is_ok());
    ASSERT_TRUE(session.add_node("Branch", "br").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "logTrue").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "logFalse").is_ok());
    ASSERT_TRUE(session.add_event("Run").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "context", "start", "br", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "br", "onTrue", "logTrue", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "br", "onFalse", "logFalse", "enter").is_ok());
    ASSERT_TRUE(session.add_link("Run", "adder", "A", "a").is_ok());
    ASSERT_TRUE(session.add_link("Run", "adder", "B", "b").is_ok());
    ASSERT_TRUE(session.add_link("Run", "cmp", "A", "adder", "Result").is_ok());
    ASSERT_TRUE(session.add_link("Run", "br", "condition", "cmp", "IsGreater").is_ok());

    // Graph 2: Sequence + Loop
    ASSERT_TRUE(session.new_graph("SequenceLoop").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "count", "int").is_ok());
    ASSERT_TRUE(session.add_node("Sequence", "seq").is_ok());
    ASSERT_TRUE(session.add_node("ForLoop", "loop").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "logA").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "logB").is_ok());
    ASSERT_TRUE(session.add_node("IntToString", "idxStr").is_ok());
    ASSERT_TRUE(session.add_event("Run").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "context", "start", "seq", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "seq", "then0", "loop", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "loop", "loopBody", "logA", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "seq", "then1", "logB", "enter").is_ok());
    ASSERT_TRUE(session.add_link("Run", "loop", "lastIndex", "count").is_ok());
    ASSERT_TRUE(session.add_link("Run", "idxStr", "value", "loop", "index").is_ok());
    ASSERT_TRUE(session.add_link("Run", "logA", "message", "idxStr", "result").is_ok());

    // Graph 3: Spawn + Physics
    ASSERT_TRUE(session.new_graph("SpawnAndTrace").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "origin", "FVector").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "dir", "FVector").is_ok());
    ASSERT_TRUE(session.add_node("SpawnActorFromClass", "spawn").is_ok());
    ASSERT_TRUE(session.add_node("LineTraceByChannel", "trace").is_ok());
    ASSERT_TRUE(session.add_node("DrawDebugLine", "dbgLine").is_ok());
    ASSERT_TRUE(session.add_node("DrawDebugSphere", "dbgSphere").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "hitLog").is_ok());
    ASSERT_TRUE(session.add_event("Fire").is_ok());
    ASSERT_TRUE(session.add_flow("Fire", "context", "start", "spawn", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Fire", "spawn", "exit", "trace", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Fire", "trace", "hit", "dbgSphere", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Fire", "dbgSphere", "exit", "hitLog", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Fire", "trace", "noHit", "dbgLine", "enter").is_ok());
    ASSERT_TRUE(session.add_link("Fire", "spawn", "location", "origin").is_ok());
    ASSERT_TRUE(session.add_link("Fire", "trace", "start", "origin").is_ok());
    ASSERT_TRUE(session.add_link("Fire", "trace", "end", "dir").is_ok());
    ASSERT_TRUE(session.add_link("Fire", "dbgSphere", "center", "trace", "hitLocation").is_ok());
    ASSERT_TRUE(session.add_link("Fire", "dbgLine", "start", "origin").is_ok());
    ASSERT_TRUE(session.add_link("Fire", "dbgLine", "end", "dir").is_ok());

    // Graph 4: Timer + Gate
    ASSERT_TRUE(session.new_graph("TimerGate").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "interval", "float").is_ok());
    ASSERT_TRUE(session.add_node("SetTimer", "timer").is_ok());
    ASSERT_TRUE(session.add_node("Gate", "gate").is_ok());
    ASSERT_TRUE(session.add_node("DoOnce", "once").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "tickLog").is_ok());
    ASSERT_TRUE(session.add_event("Start").is_ok());
    ASSERT_TRUE(session.add_flow("Start", "context", "start", "timer", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Start", "timer", "onTimer", "gate", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Start", "gate", "exit", "once", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Start", "once", "completed", "tickLog", "enter").is_ok());
    ASSERT_TRUE(session.add_link("Start", "timer", "time", "interval").is_ok());

    // Graph 5: Animation + UI
    ASSERT_TRUE(session.new_graph("AnimUI").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "character", "ACharacter").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "montage", "UAnimMontage").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "widget", "UUserWidget").is_ok());
    ASSERT_TRUE(session.add_node("PlayAnimMontage", "playAnim").is_ok());
    ASSERT_TRUE(session.add_node("SetTextBlock", "setText").is_ok());
    ASSERT_TRUE(session.add_node("SetVisibility", "showWidget").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "animLog").is_ok());
    ASSERT_TRUE(session.add_event("Play").is_ok());
    ASSERT_TRUE(session.add_flow("Play", "context", "start", "playAnim", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Play", "playAnim", "exit", "setText", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Play", "setText", "exit", "showWidget", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("Play", "showWidget", "exit", "animLog", "enter").is_ok());
    ASSERT_TRUE(session.add_link("Play", "playAnim", "target", "character").is_ok());
    ASSERT_TRUE(session.add_link("Play", "playAnim", "montage", "montage").is_ok());
    ASSERT_TRUE(session.add_link("Play", "setText", "widget", "widget").is_ok());
    ASSERT_TRUE(session.add_link("Play", "showWidget", "widget", "widget").is_ok());

    auto& mod = session.module();
    ASSERT_EQ(mod.graphs.size(), 5u);

    // Count total nodes across all graphs
    size_t total_nodes = 0;
    for (auto& g : mod.graphs) total_nodes += g.node_instances.size();
    EXPECT_GT(total_nodes, 20u);

    // Round-trip the entire module
    Environment env2;
    load_all(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    // Bake every graph
    for (auto& g : mod.graphs) {
        auto eg = EditGraph::build(g, env);
        auto rg = RuntimeGraph::bake(eg);
        EXPECT_GT(rg.node_count(), 0u);
    }

    // Full dump for diagnostic
    std::string full_dump = debug::dump_module(mod);
    EXPECT_GT(full_dump.size(), 1000u);
}
