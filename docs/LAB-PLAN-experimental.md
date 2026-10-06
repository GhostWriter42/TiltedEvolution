# Lab test plan: `experimental` (code @ `a09394dd`)

Copied from the shared review BOARD (`## Lab test plan (experimental)`), steps written by STR Quest, STR Party and STR Scenes.

**Before you start (lab only):**
1. For lab **client** builds only, apply `docs/lab-client-debug-logging.patch` with `git apply docs/lab-client-debug-logging.patch` (in `Code/client/TiltedOnlineApp.cpp` it uncomments `rotatingLogger->set_level(spdlog::level::debug)` and adds `logger->set_level(spdlog::level::debug)`; the logger line is needed because spdlog 1.13 loggers default to info, so the sink line alone changes nothing). Stock clients log only info, warn and error, and most client proof lines below are debug level.
2. Run the server with `sLogLevel=debug`.
3. **Never ship a client built with that patch.** `experimental` itself stays unpatched. Steps without a log line keep their watch-the-game checks.

Target: `fork/experimental`, code @ `a09394dd` (Q1–Q8, P1–P7, S1–S13 written against `7146f55c`; Q9, P8, S14 cover round 4). Client and server must come from the same build. Format per step: number, clients needed (2 or 3), what the player does, what should happen, and the log line that proves it.

### Quest steps (Q1, Q2, ...)

Build `fork/experimental` @ `7146f55c`; **client and server from the same build** (#848 shifts opcodes). Log level **debug** on the server and both clients; lines below are `spdlog::debug` unless marked. The prefix is `__FUNCTION__` as MSVC prints it: client `QuestService::OnEvent` / `QuestService::ApplyQuestUpdate` (`Code/client/Services/Generic/QuestService.cpp`, **C-QS**), server `QuestService::OnQuestChanges` / `QuestService::OnQuestSceneChanges` (`Code/server/Services/QuestService.cpp`, **S-QS**). Status numbers in server lines: 0 StageUpdate, 1 Started, 2 Stopped. A = leader, B = member. "Re-apply" = Party panel → **Guest desync recovery** → **Re-apply cached party quest updates**.

**Q1 Re-apply of a cached Started entry is not echoed (4be69bda).** Clients: 2.
- Setup: A and B in a party. A starts a new quest Q and does **not** advance it; B receives the start. B runs console `stopquest <Q editor id>` (an incomplete member stop isn't synced, see Q3). Wait > 30 s (server dedup TTL).
- Player does: B presses Re-apply.
- Expected: Q is running again on B; nothing about Q goes from B to the server.
- Proof: B info `Guest recovery: reapplied {} cached quest update(s)` (`Code/client/Services/Debug/Views/PartyView.cpp`) and C-QS info `Reapplied {} cached party quest update(s) for guest desync recovery`, then C-QS `QuestService::OnEvent: suppressing resync echo start/stop formId: {:X}, started: {}, player {}` with `started: true` for Q. S-QS: **no** `QuestService::OnQuestChanges: SendToLeader quest: {:X}, stage: {}, status: {}, by {} {:X}` from B for Q after the press (nor `QuestService::OnQuestChanges: SendToLeader dropping duplicate quest: {:X}, stage: {}, status: {}, by {} {:X}`; either one means the echo got out).

**Q2 Re-apply of a cached StageUpdate entry is not echoed (4be69bda, 71799693).** Clients: 2.
- Setup: as Q1, but A advances Q to stage N first (B follows), then B `stopquest`s Q. Wait > 30 s.
- Player does: B presses Re-apply.
- Expected: B is back at stage N.
- Proof: C-QS (B) `QuestService::OnEvent: suppressing resync echo stage formId: {:X}, questStage: {}, player {}` with stage N. S-QS: no `QuestService::OnQuestChanges: SendToLeader quest: {:X}, stage: {}, status: {}, by {} {:X}` from B with status 0 for Q at N. Known: the quest-start event of this restart isn't recorded for StageUpdate entries, so a status-1 `SendToLeader quest:` line from B may still appear; A's C-QS then logs `QuestService::ApplyQuestUpdate: suppressing duplicate start {} formId: {:X}, questStage: {}, questType: {}, isStopped: {}, flags: {:X}, {} {}, name: {}`. Also run Re-apply once with B already at N: no `suppressing resync echo stage` line and no error (same-stage entries are a no-op).
- Stopped-echo fix 7146f55c: see Scenes' S-step.

**Q3 Member incomplete stop not synced; member turn-in still synced (#846 IsCompleted).** Clients: 2.
- Setup: A and B in a party, both on a shared quest Q.
- Player does: (a) B fails/abandons Q, or console `stopquest`, while incomplete. (b) Separately, B completes a shared quest (turn-in).
- Expected: (a) A keeps Q; (b) A's Q completes too.
- Proof: (a) C-QS (B) `QuestService::OnEvent: member not syncing incomplete stop formId: {:X}, {} {}, name: {}`; no S-QS `{}: stopped quest: {:X}, stage: {}, by {} {:X}` from B. (b) C-QS (B) info `QuestService::OnEvent: {} {} formId: {:X}, questStage: {}, questType: {}, flags: {:X}, {} {}, name: {}` with `stopped`; S-QS `QuestService::OnQuestChanges: stopped quest: {:X}, stage: {}, by {} {:X}` with `player`; A's C-QS `QuestService::ApplyQuestUpdate: remotely stopped {} formId: {:X}, questStage: {}, questType: {}, isStopped: {}, flags: {:X}, {} {}, name: {}`.

**Q4 Member stage goes member → leader → party once (#848 dedup).** Clients: 2 (3 to see the party loop skip/send per member).
- Setup: A and B (and C) in a party on quest Q.
- Player does: B advances Q one stage.
- Expected: everyone reaches the stage once; no repeat storm.
- Proof: S-QS `QuestService::OnQuestChanges: SendToLeader quest: {:X}, stage: {}, status: {}, by {} {:X}` (by player B). Then, from A's echo: `QuestService::OnQuestChanges: SendToParty: quest: {:X}, stage: {}, status: {}, by {} {:X}` (by leader A), followed by `QuestService::OnQuestChanges: SendToParty skipping duplicate send quest: {:X}, stage: {}, status: {}, SceneMaster {}, to player {:X}` for A and B, and `QuestService::OnQuestChanges: SendToParty sending quest: {:X}, stage: {}, status: {}, SceneMaster {}, to player {:X}` for C. Any later reflection of the same stage: `QuestService::OnQuestChanges: SendToLeader dropping duplicate quest: {:X}, stage: {}, status: {}, by {} {:X}` or `QuestService::OnQuestChanges: SendToParty dropping duplicate: quest: {:X}, stage: {}, status: {}, by {} {:X}`. Fail: the same quest+stage sent to a player twice within 30 s. After ~30 s, on the next quest traffic: `QuestStageDedupHistory::Expire: expiring dedup history entry quest/scene: {:X}, stage: {}, status: {}, by player {:X}`.

**Q5 Same-stage remote update is quiet (71799693).** Clients: 2.
- Setup: A and B together on a quest scene that sets a stage when it ends. A triggers it (A becomes SceneMaster); B watches it in range so B also reaches the stage.
- Player does: let the scene end.
- Expected: B stays at the stage; no errors.
- Proof: S-QS `QuestService::OnQuestSceneChanges: SceneMaster {} {} sending force current quest stage {} to party quest {:X}, scene {:X}, scenetype {}`, then `QuestService::OnQuestChanges: SendToParty sending quest: {:X}, stage: {}, status: {}, SceneMaster {}, to player {:X}` to B. C-QS (B) `QuestService::ApplyQuestUpdate: remotely updated {} formId: {:X}, questStage: {}, questType: {}, isStopped: {}, flags: {:X}, {} {}, name: {}` with **no** following error `QuestService::ApplyQuestUpdate: failed to remotely update status {} {} formId: {:X}, questStage: {}, questType: {}, isStopped: {}, flags: {:X}, {} {}, name: {}` and no warn `TESQuest::ScriptSetStage: returned false quest formId {:X}, currentStage {}, newStage {}, name {}`.

**Q6 Quest cache only filled in a party, emptied on leave (f4210dc9).** Clients: 2.
- Setup: A and B in a party; A advances a quest so B's Party panel shows `Cached party quest updates: N` (N > 0).
- Player does: B leaves; A keeps advancing quests (solo party); B plays solo a bit, then rejoins A.
- Expected: right after rejoin, B's panel shows `Cached party quest updates: 0` until A sends something new.
- Proof: no log; verify by observation (the count is only visible while in a party, so read it right after rejoin). The late-in-flight race itself can't be forced by hand.

**Q7 Resync echo records cleared on party leave (c95039a6).** Clients: 2. Timing: within 5 s.
- Setup: as Q2 (B has re-applied quest Q at stage N).
- Player does: B presses Re-apply, leaves the party within 5 s, rejoins, then advances Q to N+1 itself.
- Expected: B's own advance is sent.
- Proof: S-QS `QuestService::OnQuestChanges: SendToLeader quest: {:X}, stage: {}, status: {}, by {} {:X}` from B with N+1; C-QS (B) has no `QuestService::OnEvent: suppressing resync echo stage formId: {:X}, questStage: {}, player {}` for N+1 after the rejoin.

**Q8 Server survives a member leaving mid-update (2572161d).** Clients: 2. Best-effort, timing-dependent.
- Setup: A and B in a party on quest Q.
- Player does: B triggers a stage advance and immediately presses Leave (repeat a few times; also with A leaving right after B advances).
- Expected: server keeps running; B's quest log update is still recorded.
- Proof: S-QS `QuestService::OnQuestChanges: updated quest: {:X}, stage: {}, SceneMaster {}, by {} {:X}` for B. The no-party guard itself has no log; pass = no server crash. If the leader vanishes, warn `QuestService::OnQuestChanges: SendToLeader no leader {} found for party, dropping quest: {:X}, stage: {}, status: {}, by {} {:X}` (acceptable, no crash).
- Not lab-testable with stock clients: null-scene guard (1c189625, pass = no crash during Q5) and the unknown-status drop (a1768632; error `QuestService::OnQuestChanges: unknown quest status {}, dropping quest: {:X}, stage: {}, by {} {:X}` should never appear).

**Q9 Filtered Re-apply Started leaves no echo record (round 4, `8107edb9`, staged as `24a07d59`).** Clients: 2. Needs the debug-logging client.
- Setup: B is in A's party with a cached leader Started entry for quest Q at stage N. On B, console `stopquest` Q so that B's current stage is N (or a later stage with N already done).
- Player does: B presses Re-apply, then within 5 s really restarts Q (`startquest` or `setstage`).
- Expected: Re-apply is filtered and records nothing, so B's real restart is sent.
- Proof: C-QS (B) warn `TESQuest::ScriptSetStage: returned false quest formId {:X}, currentStage {}, newStage {}, name {}` on Re-apply; then info `QuestService::OnEvent: started {} formId: {:X}, questStage: {}, questType: {}, flags: {:X}, {} {}, name: {}` for Q, with NO debug `QuestService::OnEvent: suppressing resync echo start/stop formId: {:X}, started: {}, player {}` for Q. S-QS `QuestService::OnQuestChanges: SendToLeader quest: {:X}, stage: {}, status: {}, by {} {:X}` from B. Before `8107edb9` the restart was swallowed.
- Reset case unchanged (engine-internal events): if `REPORT THIS LOG, experimental remote reset` shows during Re-apply, capture any suppress lines in the next 5 s.

Lab-only, not on experimental: party-wide scene-end broadcast `d3fb4bc6` (3 clients), see **STR Quest — lab-only: party-wide scene-end stage broadcast**; remaining Started-case gap (filter passes but Papyrus `SetCurrentStageID` returns false: record lingers ≤5 s) and the Reset-case record.

### Party steps (P1, P2, ...)

Build `fork/experimental` @ `7146f55c`. **Server log level debug**: every server line below is `spdlog::debug` unless marked warn. Server logs come from `Code/server/Services/PartyService.cpp` (**S-PS**), client logs from `Code/client/Services/Generic/PartyService.cpp` (**C-PS**). Use the in-game debug **Party** panel (ImGui `PartyView`). Already covered elsewhere, don't repeat: **runbook P5** (#8 leader-change claims) and **runbook P6** (#5 recovery buttons) in the Lab runbook Pass/Fail table.

**P1 Kick, leave, leader reassign (regression for the lookup/kick guards).** Clients: 2, or 3 with C optional.
- Setup: A creates a party and invites B; B accepts.
- Player does: A presses **Kick** on B. Then B rejoins, and A presses **Leave**.
- Expected: B is out of the party. When A leaves, B becomes leader. No crash.
- Proof: S-PS `[PartyService]: Kicking player {} from party`, then `[PartyService]: Sending party left event to player.`; C-PS (B) `[PartyService]: Left party`. On A's leave: S-PS `[PartyService]: Leader left, reassigned party leader to {}`.
- Guard lines (S-PS `[PartyService]: Player {} is not in the kicker's party. Cannot kick` and warn `[PartyService]: Player {} had stale party id {}, clearing it.`): these should **never** appear in normal play. The UI shows **Kick** only to the leader, and only for own-party members. Non-leader and cross-party kicks need a crafted request, so they can't be tested from the UI. A non-leader kick has **no log line** after `[PartyService]: Received request to kick party member {}`.

**P2 Old invite can't be reused after leaving (464efa4c).** Clients: 2.
- Setup: A creates a party and invites B; B accepts within 60 s.
- Player does: B presses **Leave**, then within 60 s presses **Accept** on A's invite. It's still listed, because the client keeps invites until they expire.
- Expected: B is **not** re-added. A fresh invite from A still works.
- Proof: S-PS `[PartyService]: Got party accept request from {}` with **no** following `[PartyService]: Invite found, processing.` (the invite was consumed; the server returns silently, no log line).

**P3 Invite expiry about 60 s (893d5ec9 + 87556706).** Clients: 2.
- Setup: A creates a party and invites B. B does nothing.
- Player does: wait about 60–70 s, then look at B's panel.
- Expected: the invite disappears from B's panel (client expiry). The server also drops it, within its 10 s purge.
- Proof: no log line for the expiry itself. Observe the invite vanishing in the Party panel. The server-side reject on accept (S-PS `[PartyService]: Invite expired, cancelling.`) only fires if the client's clock lags the server tick, because the client gate stops **Accept** after expiry. Treat it as best-effort: it should appear only with skew, never a join.

**P4 Inviter disconnects with a pending invite.** Clients: 2.
- Setup: A creates a party and invites B.
- Player does: A quits or disconnects. B presses **Accept** on A's invite.
- Expected: nothing happens on B and no server crash. B can still create or join other parties.
- Proof: S-PS `[PartyService]: Got party accept request from {}` with no further party lines (the inviter is gone: no log line).

**P5 Disconnect clears declined-ownership blacklist (e96ebb75).** Clients: 2. Best-effort: depends on pointer reuse.
- Setup: A owns NPCs in a cell. B arrives while the cell is still loading, so B declines a grant. B then disconnects and reconnects, and stays in the cell.
- Player does: A disconnects (or walks far away).
- Expected: A's NPCs hand off to B; they aren't despawned.
- Proof: client (B) `Declined ownership of actor {:X} at epoch {} because the actor is not ready` (`Code/client/Services/Generic/CharacterService.cpp`). Later on the server: `Transferred ownership of actor {:X} from player {:X} to player {:X} for {} (epoch {} to {})` to B, **not** `Removing actor {:X} after {} because no eligible owner remains` (`Code/server/Services/CharacterService.cpp`).

**P6 SceneMaster cleared when it leaves, is kicked, or disconnects (a1155c9e).** Clients: 2, or 3 to see a remaining member take over.
- Setup: use the scene and setup from **STR Quest — lab-only: party-wide scene-end stage broadcast → Lab test** step 1 (member B triggers the scene and becomes SceneMaster). Don't duplicate those steps.
- Player does: mid-scene, B leaves the party. Repeat with A kicking B, then with B disconnecting.
- Expected: the server clears the master. The next scene Begin from a remaining member elects a new master.
- Proof: S-PS `[PartyService]: SceneMaster {} left party {}, clearing SceneMaster.`. On the next Begin: `Code/server/Services/QuestService.cpp` `{}: quest {:X}, scene {:X}, sceneType {}, {} {} is now SceneMaster` naming the remaining player (and **not** `... already have SceneMaster player {}` with B's id).

**P7 Client reconnect state (56c0c78c) and party info guard (486b072a).** Clients: 2.
- Setup: A and B are online; A creates a party.
- Player does: B disconnects, opens the Party panel while offline, reconnects, and is invited again.
- Expected: no crash in the panel while offline. After reconnect, A's **Other Players** shows only currently online players. The leader is shown correctly once B joins.
- Proof: C-PS (B) `[PartyService]: Joined party. LeaderId: {}, IsLeader: {}` with A's id. The leader id `-1` init and the cleared player list have **no log line**: observe the debug Party panel. The warn `[PartyService]: WorldEncountersEnabled global (0xB8EC1) not found` should **never** appear on a normal load order (regression check only; it can't be triggered without breaking Skyrim.esm).

**P8 Client drops a consumed invite on join (4c90a1e9, was ba0ce4be).** Clients: 2. Needs the lab debug-logging client.
- Setup: A and B are online; A creates a party.
- Player does: A invites B. B accepts, then B presses **Leave**.
- Expected: B's Party panel no longer lists A's invite (no dead **Accept**).
- Proof: C-PS (B) `[PartyService]: Joined party. LeaderId: {}, IsLeader: {}` followed by `[PartyService]: Dropped consumed invite from leader {}` with A's id.

**Known lab-only notes (not regressions):**
- After a kick, the kicked player is missing from the other players' **Other Players** list until the next join or leave. This is old behaviour: `OnPartyKick` calls `BroadcastPlayerList(pKick)`. The fix would change message routing, so it's parked.
- When the owner disconnects, NPCs can hand off to an in-range player in **another** party (`TransferToNextOwner` ignores parties). This is known and parked.

### Scenes steps (S1, S2, ...)
Build `fork/experimental` @ `7146f55c`. Roles: A = leader (owns the NPCs in the cell), B = member, C = optional second member. All in the same party and the same cell unless a step says otherwise. Turn on subtitles in every game.

**Log levels.** Each proof line is tagged with its level. The client logger sets no level (in `Code/client/TiltedOnlineApp.cpp` the `set_level(spdlog::level::debug)` line is commented out), so a stock client prints only info/warn/error. Client **[debug]** lines only appear in a client built with debug logging enabled; without that, verify those steps by observation. Server: set `sLogLevel` to `debug`. A `__FUNCTION__ ": ..."` prefix prints the function name, e.g. `CharacterService::OnDialogueEvent: ...`. Client lines come from `Code/client/Services/Generic/CharacterService.cpp` unless a step names another file.

**S1 Dialogue dedupe (87d70df0, originally 3e6bd617).** [2 clients]
- Setup / Do: outside any scene, A talks to an NPC A owns and plays several voiced lines. Then B talks to the same NPC (the #896 path, where B voices an NPC it doesn't own).
- Expect: the listening client hears each line once, from the start, with no restart or stutter. The engine calls SpeakSoundFunction twice for most lines, but the speaking client sends one request per line (the repeat within 250 ms for the same serverId and sound file is dropped).
- Proof: speaking client [debug] `CharacterService::OnDialogueEvent: dropping duplicate dialogue, serverId {:X}, soundFile {}` for most lines. It follows that client's own `CharacterService::OnDialogueEvent: isLocal {}, isPlayerDialogueSpeaker {}, isInScene {}, isSpeakingInScene {}, isTaskDialogue {}, willSync {}, scene {:X}, Actor {:X}, serverId {:X}, isLeader {}, name {}, soundFile {}` line with the same soundFile. Listening client [debug] `CharacterService::OnNotifyDialogue: playing dialogue Actor {:X}, serverId {:X}, isLeader {}, name {}, soundFile {}` exactly **once** per line.
- Fail: two `playing dialogue` lines with the same soundFile a fraction of a second apart, or the line audibly restarting.

**S2 Synced subtitle backstop expires at 30 s (162778b0, originally bf186c10).** [2 clients]
- Setup / Do: A talks to an NPC A owns. B stands close and watches B's subtitle. Wait 30–35 s after the line.
- Expect: B shows the line's subtitle. There is no "hide subtitle" message on the wire. Instead, about 30 s after B received the subtitle, B force-hides anything still showing for that actor. Normally the game has already hidden it, so nothing visible happens; a stuck subtitle clears at about 30 s. A newer synced line from the same actor restarts the 30 s window.
- Proof: B [debug] `CharacterService::OnNotifySubtitle: showing subtitle Actor {:X}, serverId {:X}, isLeader {}, name {}, message: {}`. About 30 s after that actor's **last** line, B logs [debug] `CharacterService::RunSubtitleTimeouts: hiding expired synced subtitle, formId {:X}` with the same formId.

**S3 Synced subtitle is hidden when the actor leaves (162778b0).** [2 clients]
- Setup / Do: A starts a long voiced line from an NPC A owns. While B's subtitle is on screen, B leaves quickly (load door or fast travel) so the NPC unloads for B.
- Expect: B's subtitle for that NPC disappears immediately, not 30 s later. No crash.
- Proof: no log line for this hide (the actor-removed path doesn't log); verify by observation. Supporting evidence: B logs **no** later `CharacterService::RunSubtitleTimeouts: hiding expired synced subtitle, formId {:X}` for that formId, because the entry was dropped on removal.

**S4 A local subtitle cancels the backstop (162778b0).** [2 clients]
- Setup / Do: B receives a synced line from NPC X while A talks to X. Within 30 s, B starts its own conversation with X, so B's game shows X's subtitles locally.
- Expect: B's own conversation subtitles stay up for their full length. The earlier synced line's 30 s backstop is cancelled and does not cut them.
- Proof: B [debug] `CharacterService::OnSubtitleEvent: isLocal {}, isPlayerDialogueSpeaker {}, isInScene {}, isSpeakingInScene {}, isTaskDialogue {}, willSync {}, scene {:X}, Actor {:X}, serverId {:X}, isLeader {}, name {}, subtitle {}` for X, and **no** `CharacterService::RunSubtitleTimeouts: hiding expired synced subtitle, formId {:X}` for X about 30 s after the earlier `showing subtitle`. The cancel itself has no log line.

**S5 Disconnect clears synced subtitles and dedupe state (62f2f7ba, originally f89dca65).** [2 clients]
- Setup / Do: A starts a long voiced line. While B's synced subtitle is on screen, B disconnects. B reconnects, rejoins the party, and repeats S1 once.
- Expect: the subtitle disappears immediately on disconnect, not after 30 s. After reconnecting, synced dialogue works normally, and no line is dropped as a duplicate of something from the old session.
- Proof: B [warn] `Disconnected from server {}` (`Code/client/Services/Generic/TransportService.cpp`), followed right away by [debug] `CharacterService::RunSubtitleTimeouts: hiding expired synced subtitle, formId {:X}`, well under 30 s after the matching `showing subtitle`. The dedupe reset has no log line; verify by observation that after reconnecting, the S1 pattern holds (a `dropping duplicate dialogue` line only ever follows that client's own send of the same soundFile).

**S6 Receive guards: deleted or despawned actor, empty sound file (a22e586f, originally 77a81ef8).** [2 clients]
- Setup / Do: A starts a voiced line from an NPC A owns while B watches. Mid-line, A kills the NPC or uses console `disable` on it. Repeat with B walking out of range mid-line. Messages with an empty sound file, or aimed at B's own player, have no legitimate sender, so normal play can't produce them; this step is a regression check for them.
- Expect: no crash on either client. B's subtitle clears (S3 / S2 backstop). Nobody's own conversation is cut off. Nothing stays stuck on screen.
- Proof: skipping a deleted actor or an empty sound file has **no log line**; verify by observation. If B can no longer resolve the actor, the existing nearby line is [debug] `{}: form not found for server id {:X}` (`Code/client/Utils.h`, `GetByServerId`). The local-player guard logs [warn] `CharacterService::OnNotifyDialogue: ignoring dialogue for the local player, serverId {:X}`; it should **never** appear in normal play.

**S7 Beast form while offline or mid-connect (ab1ac3cb).** [2 clients]
- Setup / Do: (a) B runs the STR client without connecting to a server and enters werewolf (or Vampire Lord), then leaves. (b) B connects and transforms during the connect/loading window, or right after connecting before joining the party.
- Expect: no crash in either case. After B connects, A sees B spawn with B's current appearance.
- Proof: (a) **no log line** (the beast-form handler returns silently when not connected); verify by observation. (b) If the local player isn't registered yet: [warn] `CharacterService::OnBeastFormChange: local player entity not found`, or the existing [error] `{}: failed to find server id` (prefix `CharacterService::OnBeastFormChange`). Either is a pass as long as there's no crash.

**S8 Beast form syncs while connected (#914 cdc5745, merged as b18e2134; plus ab1ac3cb).** [2 clients]
- Setup: A and B in the same party, standing in visual range (same cell preferred). Try both roles as transformer.
- Do: the transformer enters werewolf or Vampire Lord. **Checkpoint A:** the remote client shows the beast mesh within about 1 s. The transformer then leaves beast form and waits up to about 1 s (detection polls every 250 ms). **Checkpoint B:** the remote client shows the humanoid appearance, not a stuck beast. Confirm the transform quests still don't sync stages (`0x2BA16` / `0x20071D0` denylist): the remote journal must not jump because of the transform itself. Repeat with the other client as transformer.
- Expect: both checkpoints pass. A brief wrong-mesh flash under 250 ms that then corrects itself is a soft pass (poll delay by design). A stuck beast mesh after leaving is a fail.
- Proof: the transform detection has no log line. The watching client logs [info] `CharacterSpawnRequest, server id: {:X}, form id: {:X}, dead: {}` once after the enter and once after the leave (the respawn round trip). Fail lines: watcher [error] `Actor to respawn not found in: {:X}` or [warn] `Character with remote id {:X} is already spawned.`; server [warn] `No OwnerComponent found for actor id {:X}` (`Code/server/Services/CharacterService.cpp`).

**S9 #854 scene voicing with the #896 speaker gate (7467db30 + b15ef193).** [2 clients]
- Setup / Do: (a) Outside a scene, B talks to an NPC A owns (B is the conversation player but not the owner). (b) With both players in range, let an ambient scripted scene play (NPCs talking to each other, no dialogue menu), for example a leader-triggered scene. (c) During a scene, B answers a dialogue menu (TaskDialogue).
- Expect: (a) A hears B's NPC lines once. (b) In-scene non-TaskDialogue lines are not sent; each client plays the scene locally, and the scene finishes on both with no phase hang. (c) TaskDialogue lines sync to A.
- Proof: (a) B [debug] `CharacterService::OnDialogueEvent: isLocal {}, isPlayerDialogueSpeaker {}, ...` with `isLocal false, isPlayerDialogueSpeaker true` and `willSync true`; A [debug] `CharacterService::OnNotifyDialogue: playing dialogue Actor {:X}, serverId {:X}, isLeader {}, name {}, soundFile {}`. (b) the owner's OnDialogueEvent line shows `isInScene true`, `isTaskDialogue false`, `willSync false`, and the other client has **no** `playing dialogue` line for those soundFiles. (c) `isTaskDialogue true` and `willSync true`. "No stall" has no log line; verify by observation. Known gap: a member-triggered scene that needs NPC AI packages can still stall (packages run on the leader only). Record it and recover with S11.

**S10 A bystander hears a member-voiced line once (#896 / #854 + 87d70df0).** [3 clients]
- Setup / Do: A owns the NPC. B talks to it. C is a third party member nearby who neither owns the NPC nor is talking to it.
- Expect: C hears each of B's NPC lines exactly once.
- Proof: C [debug] `CharacterService::OnNotifyDialogue: playing dialogue Actor {:X}, serverId {:X}, isLeader {}, name {}, soundFile {}` once per soundFile. A second line for the same soundFile is the owner echo in S13 (b), not a dedupe failure: the dedupe only works within each sending client.

**S11 Guest hang fallback: Clear stuck dialogue / Unstuck player (#916 2a03b9f, merged as 69b9e363).** [2 clients]
- Setup: ideally a real hung DialogueMenu, such as a member-triggered ceremony or the S9 known gap. If there isn't one, open any NPC dialogue and use the helpers to check they clear a normal dialogue (a smoke test, not proof against a real hang). Note the current stage of any active quest involved (console `sqv` or the journal). The "Dump dialogue hang state" helper (ec00556) is **not** in this build.
- Do: (a) F3 → Helpers → **Clear stuck dialogue**. (b) Re-enter the stuck state if needed, then F3 → Helpers → **Unstuck player**.
- Expect: (a) the dialogue UI closes, the player can move, there is **no** ragdoll knock, and the quest stage is **unchanged**. (b) the same as (a) **plus** a KnockExplosion; the stage is still unchanged. Fail: still frozen, or a quest stage advanced (the helpers must never touch quests).
- Proof: **no log line** for either helper; verify by observation.

**S12 Stale Stopped echo no longer swallows a genuine stop (7146f55c, originally a06f08c0), plus a resync suppression regression check.** [2 clients]
- Setup: B (guest) is in A's party and B's Party debug panel shows `Cached party quest updates:` above 0. A completes or stops quest Q while B is in the party, so B's cache holds a Stopped entry for Q.
- Do: B presses **Re-apply cached party quest updates**. Within 5 s, B's own game stops or completes Q. A member's stop is only sent if the quest is completed (#846); an incomplete one logs [debug] `QuestService::OnEvent: member not syncing incomplete stop formId: {:X}, {} {}, name: {}`.
- Expect: B's genuine completed stop reaches the server. No resync suppression is applied to a stop.
- Proof: B [info] `Guest recovery: reapplied {} cached quest update(s)` (`Code/client/Services/Debug/Views/PartyView.cpp`) and [info] `Reapplied {} cached party quest update(s) for guest desync recovery` (`Code/client/Services/Generic/QuestService.cpp`). Then B [info] `QuestService::OnEvent: {} {} formId: {:X}, questStage: {}, questType: {}, flags: {:X}, {} {}, name: {}` with the first field `stopped`, and server [debug] `QuestService::OnQuestChanges: stopped quest: {:X}, stage: {}, by {} {:X}` (`Code/server/Services/QuestService.cpp`). B must log **no** [debug] `QuestService::OnEvent: suppressing resync echo start/stop formId: {:X}, started: {}, player {}` (with `started: false`) for Q. After this fix, the only remaining source of `started: false` suppression is re-applying a Reset entry.
- Hard to force: if B's game won't stop Q within 5 s, the minimum pass is that no `started: false` suppression line appears after re-applying a cache that contains a Stopped entry.
- Regression: re-apply Started / StageUpdate entries for quests B is behind on (A advanced them while B missed the updates, more than 30 s ago). B logs [debug] `QuestService::OnEvent: suppressing resync echo stage formId: {:X}, questStage: {}, player {}` and/or `QuestService::OnEvent: suppressing resync echo start/stop formId: {:X}, started: {}, player {}` (with `started: true`). The server shows **no** `QuestService::OnQuestChanges: SendToLeader quest: {:X}, stage: {}, status: {}, by {} {:X}` from B for those quests.

**S13 Watch items (observation only, not pass/fail).** [2 clients]
- (a) **The 30 s backstop cutting off a game-started subtitle.** On B, a subtitle the game shows by itself for an NPC A owns (for example a line from B's local copy of a scene, which raises no local subtitle event on B) vanishes early, at the moment B logs [debug] `CharacterService::RunSubtitleTimeouts: hiding expired synced subtitle, formId {:X}`, about 30 s after an earlier `showing subtitle` for the same actor. Record the actor, scene and timing.
- (b) **The #896 owning client echoes a member-voiced line back once.** B voices an NPC that A owns. Signs on A: [debug] `CharacterService::OnNotifyDialogue: playing dialogue ...` or `CharacterService::OnNotifySubtitle: showing subtitle ...`, followed by A's own `CharacterService::OnDialogueEvent: ...` or `CharacterService::OnSubtitleEvent: ...` line with `isLocal true` and `willSync true` for the same soundFile or subtitle. B then logs `playing dialogue` / `showing subtitle` for its own line (heard as a restart, or seen as a repeated subtitle). Record how often it happens.
- (c) **Known gap, not pass/fail: a re-applied Started entry can swallow a quick restart.** If a cached Started entry couldn't apply during Re-apply (the quest was already stopped or completed with that stage done), and B starts that quest within 5 s, B's start event and that stage event are swallowed. B logs [debug] `QuestService::OnEvent: suppressing resync echo start/stop formId: {:X}, started: {}, player {}` (with `started: true`) and `QuestService::OnEvent: suppressing resync echo stage formId: {:X}, questStage: {}, player {}`, while the server shows no `QuestService::OnQuestChanges:` line for that start. Workaround: don't start quests for 5 s after Re-apply. Narrowed by Quest round 4 (`24a07d59`); see Q9 for the current check and what remains.

**S14 Null/empty sound path guard in `HookSpeakSoundFunction` (a09394dd, originally afff6fab). Regression check.** [2 clients, lab debug-logging client build]
- Setup: A and B are in a party near NPCs A owns. Include a quest NPC with voiced lines and some silent or "no voice" lines (for example a follower command menu, or a mod NPC without voice files).
- Player does: repeat S1. A talks to the NPC and lets ambient NPC chatter play. Then trigger a few silent lines.
- Expected: voiced lines still sync exactly as in S1. Neither client crashes during NPC speech, including silent lines. The fix has no log line of its own.
- Proof: on B, every synced line still logs [debug] `CharacterService::OnNotifyDialogue: playing dialogue Actor {:X}, serverId {:X}, isLeader {}, name {}, soundFile {}` with a non-empty soundFile, and A never logs `CharacterService::OnDialogueEvent: ...` with an empty `soundFile`.
