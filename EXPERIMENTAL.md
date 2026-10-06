# `experimental` branch

This is an **unofficial** experimental integration branch on the
[GhostWriter42 fork](https://github.com/GhostWriter42/TiltedEvolution) of Skyrim Together Reborn.
It is not an upstream branch. It is built from upstream `dev` at `9ce2971` and stacks open upstream
PRs plus local stability fixes on top. The current tip, `7146f55c`, is compile-green on Windows
(MSVC 2022, xmake 3.1.1, `releasedbg`, all targets). It has **not been tested in-game yet**.

> **Warning: the client and server must come from the same build of this branch.** #848 inserts new
> message opcodes mid-enum, which shifts the numbering. A client from this branch can't talk to an
> upstream server, and an upstream client can't talk to a server from this branch.

## What's in

Upstream PRs: `https://github.com/tiltedphoques/TiltedEvolution/pull/N`. Lane commits were written for
this branch, and each one is listed with its short SHA on `experimental`.

### Quest sync

- [#848](https://github.com/tiltedphoques/TiltedEvolution/pull/848) quest rework (rfortier): server
  per-party `QuestStageDedupHistory` (30 s TTL), member → leader → party fan-out, SceneMaster and the
  SceneEnd forced-stage poke, new QuestScene messages. Rebased onto #854 and merged in `cc16d07d`.
- [#846](https://github.com/tiltedphoques/TiltedEvolution/pull/846) (otsffs): members don't sync
  Stopped for incomplete quests. Adds our fix `2d059c08`: `TESQuest::IsCompleted()` restores the
  completed-stop check, so turn-ins still sync. Merge `a677ff94`.
- [#915](https://github.com/tiltedphoques/TiltedEvolution/pull/915) quest-item pickup sync (draft):
  syncs persistent quest-item pickups to party members. Merge `1c5f2f6c`.
- `40429b20`: collapses the guest quest cache by GameId so only the latest update per quest is kept,
  and hardens echo handling on apply. Merge `8a0e294a`.
- `4be69bda`: suppresses quest-event echoes during a guest cache resync.
- `2572161d`: server null-party and null-leader guards in QuestService.
- `1c189625`: client null-scene guards in the scene event and cutscene check.
- `a1768632`: the server drops a `RequestQuestUpdate` with an unknown status.
- `71799693`: the client goes quiet on same-stage remote updates, and the dead unknown-status log is fixed.
- `f4210dc9`: party quest updates received outside a party are no longer cached.
- `c95039a6`: resync echo records are cleared on disconnect and party leave.
- `7146f55c`: no resync echo record for a remote Stopped. StopQuest fires no event, so a stale record
  could swallow the next real stop (bug found in cross-review).

### Party / world

- Party #8, block cross-party actor claims (`0e0379b3`, `7430da7b`, `4d1687cb`): the cross-party
  ownership claim block, clearing stale remote `OwnerPlayerId` on leave, and reclaiming on leader
  change. Merge `5a826441`.
- Party #5, guest desync recovery UX with no new opcodes (`f29d7ac1`, `25dbae2b`, `98c93471`): adds
  Re-apply/Clear cached quest updates, Refresh weather and Teleport to leader to the Party debug panel.
  Merges `40dddff8`, `dd7d36c1`.
- [#914](https://github.com/tiltedphoques/TiltedEvolution/pull/914) beast-form leave respawn (draft):
  fires `BeastFormChangeEvent` on leave after the race reverts, so others don't keep the beast model.
  Merge `b18e2134`.
- `c8124fa6`: server PartyService stale-party and cross-party kick guards (no phantom parties, kick
  limited to the kicker's own party).
- `486b072a`: the client null-checks the WorldEncountersEnabled global on party info.
- `893d5ec9`: server party invites actually expire after 60 s, and a leaver's invites are dropped.
- `e96ebb75`: disconnected players are purged from `InvalidOwners`.
- `a1155c9e`: the #848 SceneMaster is cleared when it leaves the party (leave, kick or disconnect).
- `56c0c78c`: the client PartyService initialises the leader id and clears the player list on disconnect.
- `464efa4c`: the server consumes a party invite on accept, so it can't be reused after leaving.
- `87556706`: the server rejects an expired invite on accept, without waiting for the periodic purge.

### Dialogue / scenes

- [#854](https://github.com/tiltedphoques/TiltedEvolution/pull/854) dialogue, subtitle and scene sync
  (rfortier) (`7467db30`, `f8de06c3`), plus `b15ef193`, which merges the existing #896 speaker gate
  into #854's `willSync`. Merged in `cc16d07d`.
- [#916](https://github.com/tiltedphoques/TiltedEvolution/pull/916) unstuck dialogue helpers (draft):
  debug helpers that clear stuck dialogue and re-enable controls. Merge `69b9e363`.
- `87d70df0`: drops a duplicate DialogueRequest for the same line within 250 ms.
- `162778b0`: synced subtitles no longer stick on screen (30 s backstop, hidden on actor removal).
- `ab1ac3cb`: guards OnBeastFormChange against a missing player entity.
- `5b7286bf`: drops an unused `GetCurrentScene()` call in IsSpeakingInScene.
- `62f2f7ba`: drops synced subtitles and the dedup state on disconnect.
- `a22e586f`: receive-side guards in NotifyDialogue and NotifySubtitle (deleted actors, empty sound
  file, local player).

### Build fix

- `e6327393`: uses the hopscotch iterator `value()` for mutable Party access. This fixes the MSVC
  C2440/C2664 errors from `c8124fa6`.

## Parked for the lab (not included)

- **Party-wide scene-end broadcast** `d3fb4bc6` (Quest, branch `local/str-exp-quest-broadcast`): when
  a member is SceneMaster, its forced stage reaches the whole party, so members who are behind are no
  longer stranded. It changes routing, and reproducing the original bug needs 3 clients.
- **Party #7 join-hold:** holds a joiner's quest updates until it has caught up. Any narrow version
  belongs in the QuestService forward/apply path, so it stays a note.
- **Cross-party NPC handoff:** on owner disconnect the server hands NPCs to any in-range player,
  including one from another party. Preferring same-party players changes behaviour and needs a
  2-party lab.
- **Kicked-player list resend:** `OnPartyKick` calls `BroadcastPlayerList(pKick)`, which leaves the
  kicked player off everyone's list, so the leader can't re-invite them. The fix changes who gets a
  message, so it's a routing change.
- **Scenes member-scene stall:** the real fix is SceneMaster ownership or AI-on-remote for scene
  actors. The only client-side idea depends on an unverified `fVoiceTimer` and could mute synced lines.
- **TaskDialogue goodbye lines:** there's no minimal safe approach. It hits the same goodbye-vs-first-
  scene-line ambiguity as the abandoned #854 attempt.
- **Echo fix `15ad1f2`:** a `ScopedQuestOverride` belt. It does nothing here because #848 removes the
  `IsOverriden()` early-outs.
- **Stage-step `983e98c`:** superseded by #848's server `QuestStageDedupHistory`.
- **[#839](https://github.com/tiltedphoques/TiltedEvolution/pull/839):** dropped because #848
  supersedes it. Both implement the same duplicate start/stop suppression, and #848's version wins.
- **Started-case resync echo gap (lab only):** if `ScriptSetStage` is filtered during Re-apply, the
  `(S, Started)` record stays for 5 s and can swallow a real restart. Retracting it is unsafe until a
  lab confirms the Papyrus case.

## Known risks

- **Nothing has been tested in-game.** There's no second client yet, only compiles and static review.
  Unit tests are built but haven't been run.
- The #846 rule also stops a member's real, intentional quest fail from spreading to the party.
- The resync echo guard could still swallow one real event within 5 s. Records are cleared on
  disconnect and leave, and the Stopped case is fixed in `7146f55c`, but the Started-case gap remains.
- The Scenes dialogue dedup remembers only the last line, so interleaved speakers (A, B, A) can still
  repeat.
- Party invites now really expire after 60 s and are used up on accept (an intended behaviour change).
- The existing #896 case can echo a member-voiced subtitle back once.
- [#913](https://github.com/tiltedphoques/TiltedEvolution/pull/913) will conflict with #854/#848 in
  `Actor.h` (one hunk). Both also append opcodes, so whichever lands second shifts the numbering.
- There's one new, harmless warning: C4018 (signed/unsigned) at `QuestDebugView.cpp:92`, from #848.

## Lab test plan

The lab test plan is in `docs/LAB-PLAN-experimental.md`.
Lab client builds need `docs/lab-client-debug-logging.patch` (`git apply`) to get debug-level client logs;
never ship a client built with it. Run the server with `sLogLevel=debug`.

## Building

Windows, VS 2022 (MSVC) and xmake 3.x (the repo requires ≥ 3.0.0; this branch was built with 3.1.1):

```
git submodule update --init --recursive
xmake f -m releasedbg --vs=2022 -y
xmake -y
```

The first `xmake` run downloads the pinned package dependencies. Build the client and the server
from the same commit (see the warning above).
