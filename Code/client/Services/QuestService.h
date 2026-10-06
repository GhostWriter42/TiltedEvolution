#pragma once

#include <World.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Messages/NotifyQuestUpdate.h>

struct NotifyQuestSceneUpdate;

struct TESQuest;
struct ConnectedEvent;
struct DisconnectedEvent;
struct PartyLeftEvent;

/**
 * @brief Handles quest sync
 */
class QuestService final : public BSTEventSink<TESQuestStartStopEvent>, BSTEventSink<TESQuestStageEvent>, BSTEventSink<TESSceneEvent>, BSTEventSink<TESSceneActionEvent>, BSTEventSink<TESScenePhaseEvent>
{
public:
    QuestService(World&, entt::dispatcher&);
    ~QuestService() = default;

    static bool IsNonSyncableQuest(TESQuest* apQuest);
    static void DebugDumpQuests();
    static bool StopQuest(uint32_t aformId);
    const uint32_t PlayerId() const noexcept { return m_playerId; }

    /** Party-safe guest recovery: re-apply NotifyQuestUpdate messages already received (no new net traffic). */
    size_t ReapplyCachedPartyQuestUpdates() noexcept;
    const Vector<NotifyQuestUpdate>& GetCachedPartyQuestUpdates() const noexcept { return m_partyQuestUpdateCache; }
    void ClearCachedPartyQuestUpdates() noexcept { m_partyQuestUpdateCache.clear(); }

private:
    friend struct QuestEventHandler;

    void OnConnected(const ConnectedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept; // also clears m_playerId (#848 Disconnected folded in)
    void OnPartyLeft(const PartyLeftEvent&) noexcept;

    BSTEventResult OnEvent(const TESQuestStartStopEvent*, const EventDispatcher<TESQuestStartStopEvent>*) override;
    BSTEventResult OnEvent(const TESQuestStageEvent*, const EventDispatcher<TESQuestStageEvent>*) override;
    BSTEventResult OnEvent(const TESSceneEvent*, const EventDispatcher<TESSceneEvent>*) override;
#if 1
    BSTEventResult OnEvent(const TESSceneActionEvent*, const EventDispatcher<TESSceneActionEvent>*) override;
    BSTEventResult OnEvent(const TESScenePhaseEvent*, const EventDispatcher<TESScenePhaseEvent>*) override;
#endif
    void OnQuestUpdate(const NotifyQuestUpdate&) noexcept;
    void ApplyQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept;
    void RememberPartyQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept;
    void NotifyOverlayOfQuestUpdate(uint32_t aFormId) noexcept;
    void OnQuestSceneUpdate(const NotifyQuestSceneUpdate&) noexcept;

    bool CanAdvanceQuestForParty() const noexcept;

    World& m_world;
    uint32_t m_playerId;

    static constexpr size_t kMaxCachedPartyQuestUpdates = 64;
    Vector<NotifyQuestUpdate> m_partyQuestUpdateCache;

    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_leftConnection;
    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_questSceneUpdateConnection;
};
