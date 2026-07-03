import "ue_core.d.gs";

graph HelloWorld {
    @graph.input
    param levelName: FString;
    @graph.input
    param message: FString;
    @graph.input
    param preconditionMessage: FString;
    @graph.input
    param streamingReadyMessage: FString;
    @graph.input
    param patrolConfigMessage: FString;
    @graph.input
    param convoyMessage: FString;
    @graph.input
    param anchorMessage: FString;
    @graph.input
    param monsterSpawnMessage: FString;
    @graph.input
    param npcSpawnMessage: FString;
    @graph.input
    param cartSpawnMessage: FString;
    @graph.input
    param entityBatchMessage: FString;
    @graph.input
    param entityCreatedMessage: FString;
    @graph.input
    param cartCreatedMessage: FString;
    @graph.input
    param allCreatedMessage: FString;
    @graph.input
    param teamBoundMessage: FString;
    @graph.input
    param formationMessage: FString;
    @graph.input
    param patrolStartedMessage: FString;
    @graph.input
    param patrolReachedMessage: FString;
    @graph.input
    param noNextPointMessage: FString;
    @graph.input
    param alertMessage: FString;
    @graph.input
    param regroupMessage: FString;
    @graph.input
    param resetMessage: FString;
    @graph.input
    param cleanupMessage: FString;
    @graph.input
    param patrolLeader: AActor;
    @graph.input
    param streamingPollInterval: float;
    @graph.input
    param spawnJoinDelay: float;
    @graph.input
    param waypointDelay: float;
    @graph.input
    param alertRegroupDelay: float;
    @graph.input
    param resetDelay: float;

    node resetRuntimeState {
        type PrintString;
    }
    node readLevelContext {
        type PrintString;
    }
    node activatePreconditionLayer {
        type PrintString;
    }
    node requestStreamingLoad {
        type PrintString;
    }
    node streamingPollDelay {
        type Delay;
    }
    node streamingCompleted {
        type PrintString;
    }
    node readPatrolConfig {
        type PrintString;
    }
    node spawnPointLocation {
        type GetActorLocation;
    }
    node precreateConvoyTeam {
        type PrintString;
    }
    node computeAnchorTransform {
        type PrintString;
    }
    node createSpawnBatch {
        type PrintString;
    }
    node spawnMonsterSlots {
        type PrintString;
    }
    node spawnNpcSlots {
        type PrintString;
    }
    node spawnCartSlot {
        type PrintString;
    }
    node waitSpawnCallbacks {
        type Delay;
    }
    node waitAllSpawned {
        type Delay;
    }
    node bindEntitiesToSlots {
        type PrintString;
    }
    node bindCartToTeam {
        type PrintString;
    }
    node configureTeamFormation {
        type PrintString;
    }
    node configureTeamBlackboard {
        type PrintString;
    }
    node setFirstPatrolTarget {
        type PrintString;
    }
    node registerPatrolAction {
        type PrintString;
    }
    node beginPatrolMove {
        type PrintString;
    }
    node broadcastPatrolStarted {
        type PrintString;
    }
    node printer {
        type PrintString;
    }

    node pollStreamingReady {
        type PrintString;
    }
    node pollInitPatrol {
        type PrintString;
    }
    node pollClearTimer {
        type PrintString;
    }

    node entityBatchRequested {
        type PrintString;
    }
    node batchEventSpawnMonsterSlots {
        type PrintString;
    }
    node batchEventSpawnNpcSlots {
        type PrintString;
    }
    node batchEventSpawnCartSlot {
        type PrintString;
    }
    node batchEventWaitCallbacks {
        type Delay;
    }
    node entityCreatedLog {
        type PrintString;
    }
    node cacheCreatedEntity {
        type PrintString;
    }
    node incrementCreatedCount {
        type PrintString;
    }
    node entityCreatedJoinDelay {
        type Delay;
    }
    node entityCallbackFinalize {
        type PrintString;
    }

    node cartCreatedLog {
        type PrintString;
    }
    node cacheCartEntity {
        type PrintString;
    }
    node cartCallbackFinalize {
        type PrintString;
    }
    node dispatchAllEntitiesCreated {
        type PrintString;
    }

    node allEntitiesCreatedEntry {
        type PrintString;
    }
    node attachCartToTeam {
        type PrintString;
    }
    node finalizePatrolStartup {
        type PrintString;
    }

    node advancePatrolTarget {
        type PrintString;
    }
    node patrolLoopDelay {
        type Delay;
    }
    node evaluateNextPatrolPoint {
        type PrintString;
    }
    node setNextPatrolTarget {
        type PrintString;
    }
    node patrolHeartbeat {
        type PrintString;
    }

    node clearPatrolTarget {
        type PrintString;
    }
    node waitAtRouteEnd {
        type Delay;
    }
    node resetRouteCursor {
        type PrintString;
    }

    node alertRaised {
        type PrintString;
    }
    node pausePatrolMove {
        type PrintString;
    }
    node regroupGate {
        type Delay;
    }
    node resetPatrol {
        type PrintString;
    }
    node resumePatrolMove {
        type PrintString;
    }

    node cleanupStopTimers {
        type PrintString;
    }
    node cleanupDestroyEntities {
        type PrintString;
    }
    node cleanupClearReferences {
        type PrintString;
    }
    node endPlayCleanup {
        type PrintString;
    }

    // 对标 LevelScriptActor_Xibei_NPC_PatrolTeam.as:
    // ECSBeginPlayBP -> ResetRuntimeState -> streaming poll -> InitPatrolAndSpawn -> CreateEntityBatch.
    event OnStart {
        connect(context.start, resetRuntimeState.enter);
        connect(resetRuntimeState.exit, readLevelContext.enter);
        connect(readLevelContext.exit, activatePreconditionLayer.enter);
        connect(activatePreconditionLayer.exit, requestStreamingLoad.enter);
        connect(requestStreamingLoad.exit, streamingPollDelay.enter);
        connect(streamingPollDelay.completed, streamingCompleted.enter);
        connect(streamingCompleted.exit, readPatrolConfig.enter);
        connect(readPatrolConfig.exit, precreateConvoyTeam.enter);
        connect(precreateConvoyTeam.exit, computeAnchorTransform.enter);
        connect(computeAnchorTransform.exit, createSpawnBatch.enter);
        connect(createSpawnBatch.exit, spawnMonsterSlots.enter);
        connect(spawnMonsterSlots.exit, spawnNpcSlots.enter);
        connect(spawnNpcSlots.exit, spawnCartSlot.enter);
        connect(spawnCartSlot.exit, waitSpawnCallbacks.enter);
        bind(message, resetRuntimeState.message);
        bind(levelName, readLevelContext.message);
        bind(preconditionMessage, activatePreconditionLayer.message);
        bind(streamingReadyMessage, requestStreamingLoad.message);
        bind(streamingPollInterval, streamingPollDelay.duration);
        bind(streamingReadyMessage, streamingCompleted.message);
        bind(patrolConfigMessage, readPatrolConfig.message);
        bind(patrolLeader, spawnPointLocation.target);
        bind(convoyMessage, precreateConvoyTeam.message);
        bind(anchorMessage, computeAnchorTransform.message);
        bind(entityBatchMessage, createSpawnBatch.message);
        bind(monsterSpawnMessage, spawnMonsterSlots.message);
        bind(npcSpawnMessage, spawnNpcSlots.message);
        bind(cartSpawnMessage, spawnCartSlot.message);
        bind(spawnJoinDelay, waitSpawnCallbacks.duration);
    }

    // DataLayer streaming timer tick: streaming 就绪后清 timer，并进入 patrol 初始化入口。
    event OnStreamingPollTick {
        connect(context.start, pollStreamingReady.enter);
        connect(pollStreamingReady.exit, pollClearTimer.enter);
        connect(pollClearTimer.exit, pollInitPatrol.enter);
        connect(pollInitPatrol.exit, createSpawnBatch.enter);
        bind(streamingReadyMessage, pollStreamingReady.message);
        bind(streamingReadyMessage, pollClearTimer.message);
        bind(patrolConfigMessage, pollInitPatrol.message);
        bind(entityBatchMessage, createSpawnBatch.message);
    }

    // 显式批次创建入口：脚本里的 CreateEntityBatch 可从 BeginPlay 或 streaming tick 进入。
    event OnCreateEntityBatch {
        connect(context.start, entityBatchRequested.enter);
        connect(entityBatchRequested.exit, batchEventSpawnMonsterSlots.enter);
        connect(batchEventSpawnMonsterSlots.exit, batchEventSpawnNpcSlots.enter);
        connect(batchEventSpawnNpcSlots.exit, batchEventSpawnCartSlot.enter);
        connect(batchEventSpawnCartSlot.exit, batchEventWaitCallbacks.enter);
        bind(entityBatchMessage, entityBatchRequested.message);
        bind(monsterSpawnMessage, batchEventSpawnMonsterSlots.message);
        bind(npcSpawnMessage, batchEventSpawnNpcSlots.message);
        bind(cartSpawnMessage, batchEventSpawnCartSlot.message);
        bind(spawnJoinDelay, batchEventWaitCallbacks.duration);
    }

    // OnEntityCreateFinish: 每个实体生成回调计数，满足总数后触发 all-created 汇合。
    event OnEntityCreateFinish {
        connect(context.start, entityCreatedLog.enter);
        connect(entityCreatedLog.exit, cacheCreatedEntity.enter);
        connect(cacheCreatedEntity.exit, incrementCreatedCount.enter);
        connect(incrementCreatedCount.exit, entityCreatedJoinDelay.enter);
        connect(entityCreatedJoinDelay.completed, entityCallbackFinalize.enter);
        connect(entityCallbackFinalize.exit, dispatchAllEntitiesCreated.enter);
        bind(entityCreatedMessage, entityCreatedLog.message);
        bind(entityCreatedMessage, cacheCreatedEntity.message);
        bind(allCreatedMessage, incrementCreatedCount.message);
        bind(spawnJoinDelay, entityCreatedJoinDelay.duration);
        bind(allCreatedMessage, entityCallbackFinalize.message);
        bind(allCreatedMessage, dispatchAllEntitiesCreated.message);
    }

    // OnCartEntityCreateFinish: 马车创建后绑定到队伍，再复用 all-created 汇合。
    event OnCartEntityCreateFinish {
        connect(context.start, cartCreatedLog.enter);
        connect(cartCreatedLog.exit, cacheCartEntity.enter);
        connect(cacheCartEntity.exit, bindCartToTeam.enter);
        connect(bindCartToTeam.exit, cartCallbackFinalize.enter);
        connect(cartCallbackFinalize.exit, dispatchAllEntitiesCreated.enter);
        bind(cartCreatedMessage, cartCreatedLog.message);
        bind(cartSpawnMessage, cacheCartEntity.message);
        bind(teamBoundMessage, bindCartToTeam.message);
        bind(entityCreatedMessage, cartCallbackFinalize.message);
        bind(allCreatedMessage, dispatchAllEntitiesCreated.message);
    }

    // OnAllEntitiesCreated: 完成实体绑定、编队、黑板初始化并启动巡逻行为。
    event OnAllEntitiesCreated {
        connect(context.start, allEntitiesCreatedEntry.enter);
        connect(allEntitiesCreatedEntry.exit, waitAllSpawned.enter);
        connect(waitAllSpawned.completed, bindEntitiesToSlots.enter);
        connect(bindEntitiesToSlots.exit, attachCartToTeam.enter);
        connect(attachCartToTeam.exit, configureTeamFormation.enter);
        connect(configureTeamFormation.exit, configureTeamBlackboard.enter);
        connect(configureTeamBlackboard.exit, setFirstPatrolTarget.enter);
        connect(setFirstPatrolTarget.exit, registerPatrolAction.enter);
        connect(registerPatrolAction.exit, beginPatrolMove.enter);
        connect(beginPatrolMove.exit, broadcastPatrolStarted.enter);
        connect(broadcastPatrolStarted.exit, printer.enter);
        bind(allCreatedMessage, allEntitiesCreatedEntry.message);
        bind(spawnJoinDelay, waitAllSpawned.duration);
        bind(teamBoundMessage, bindEntitiesToSlots.message);
        bind(teamBoundMessage, attachCartToTeam.message);
        bind(formationMessage, configureTeamFormation.message);
        bind(patrolConfigMessage, configureTeamBlackboard.message);
        bind(patrolStartedMessage, setFirstPatrolTarget.message);
        bind(patrolStartedMessage, registerPatrolAction.message);
        bind(patrolStartedMessage, beginPatrolMove.message);
        bind(patrolStartedMessage, broadcastPatrolStarted.message);
        bind(message, printer.message);
    }

    // OnPatrolActionEvent: ReachTeamTarget 后计算下一巡逻点并设置 TeamEntity 目标。
    event OnPatrolActionEvent {
        connect(context.start, advancePatrolTarget.enter);
        connect(advancePatrolTarget.exit, patrolLoopDelay.enter);
        connect(patrolLoopDelay.completed, evaluateNextPatrolPoint.enter);
        connect(evaluateNextPatrolPoint.exit, setNextPatrolTarget.enter);
        connect(setNextPatrolTarget.exit, patrolHeartbeat.enter);
        bind(patrolReachedMessage, advancePatrolTarget.message);
        bind(waypointDelay, patrolLoopDelay.duration);
        bind(patrolReachedMessage, evaluateNextPatrolPoint.message);
        bind(patrolStartedMessage, setNextPatrolTarget.message);
        bind(patrolStartedMessage, patrolHeartbeat.message);
    }

    // 下一巡逻点为空时，把目标清回原点，等待后重置 route cursor 并复用首个巡逻点设置。
    event OnPatrolNoNextPoint {
        connect(context.start, clearPatrolTarget.enter);
        connect(clearPatrolTarget.exit, waitAtRouteEnd.enter);
        connect(waitAtRouteEnd.completed, resetRouteCursor.enter);
        connect(resetRouteCursor.exit, setFirstPatrolTarget.enter);
        bind(noNextPointMessage, clearPatrolTarget.message);
        bind(waypointDelay, waitAtRouteEnd.duration);
        bind(noNextPointMessage, resetRouteCursor.message);
        bind(patrolStartedMessage, setFirstPatrolTarget.message);
    }

    // 巡逻队收到告警后先暂停，等待重组，再复用 patrol target 逻辑恢复巡逻。
    event OnPatrolAlert {
        connect(context.start, alertRaised.enter);
        connect(alertRaised.exit, pausePatrolMove.enter);
        connect(pausePatrolMove.exit, regroupGate.enter);
        connect(regroupGate.completed, resetPatrol.enter);
        connect(resetPatrol.exit, resumePatrolMove.enter);
        connect(resumePatrolMove.exit, setNextPatrolTarget.enter);
        bind(alertMessage, alertRaised.message);
        bind(alertMessage, pausePatrolMove.message);
        bind(alertRegroupDelay, regroupGate.duration);
        bind(regroupMessage, resetPatrol.message);
        bind(patrolStartedMessage, resumePatrolMove.message);
        bind(patrolStartedMessage, setNextPatrolTarget.message);
    }

    // 外部关卡重置：清状态、等一帧，再回到首个巡逻点。
    event OnPatrolReset {
        connect(context.start, resetPatrol.enter);
        connect(resetPatrol.exit, waitAtRouteEnd.enter);
        connect(waitAtRouteEnd.completed, setFirstPatrolTarget.enter);
        bind(resetMessage, resetPatrol.message);
        bind(resetDelay, waitAtRouteEnd.duration);
        bind(patrolStartedMessage, setFirstPatrolTarget.message);
    }

    // ECSEndPlayBP: 停止 timer、销毁已创建实体、清引用，最后输出清理完成。
    event OnEndPlay {
        connect(context.start, cleanupStopTimers.enter);
        connect(cleanupStopTimers.exit, cleanupDestroyEntities.enter);
        connect(cleanupDestroyEntities.exit, cleanupClearReferences.enter);
        connect(cleanupClearReferences.exit, endPlayCleanup.enter);
        bind(cleanupMessage, cleanupStopTimers.message);
        bind(cleanupMessage, cleanupDestroyEntities.message);
        bind(cleanupMessage, cleanupClearReferences.message);
        bind(cleanupMessage, endPlayCleanup.message);
    }

    function GetPatrolStatus {
        connect(context.start, context.done);
        bind(patrolStartedMessage, context.result);
    }

    function GetSpawnStatus {
        connect(context.start, context.done);
        bind(allCreatedMessage, context.result);
    }

    function GetAlertStatus {
        connect(context.start, context.done);
        bind(alertMessage, context.result);
    }
}
