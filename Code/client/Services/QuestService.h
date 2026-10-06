#pragma once

#include <World.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Messages/NotifyQuestUpdate.h>

struct TESQuest;
struct ConnectedEvent;
struct DisconnectedEvent;
struct PartyLeftEvent;

/**
 * @brief Handles quest sync
 *
 * This service is currently not in use.
 */
class QuestService final : public BSTEventSink<TESQuestStartStopEvent>, BSTEventSink<TESQuestStageEvent>
{
public:
    QuestService(World&, entt::dispatcher&);
    ~QuestService() = default;

    static bool IsNonSyncableQuest(TESQuest* apQuest);
    static void DebugDumpQuests();
    static bool StopQuest(uint32_t aformId);

    /** Party-safe guest recovery: re-apply NotifyQuestUpdate messages already received (no new net traffic). */
    size_t ReapplyCachedPartyQuestUpdates() noexcept;
    const Vector<NotifyQuestUpdate>& GetCachedPartyQuestUpdates() const noexcept { return m_partyQuestUpdateCache; }
    void ClearCachedPartyQuestUpdates() noexcept { m_partyQuestUpdateCache.clear(); }

private:
    friend struct QuestEventHandler;

    void OnConnected(const ConnectedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnPartyLeft(const PartyLeftEvent&) noexcept;

    BSTEventResult OnEvent(const TESQuestStartStopEvent*, const EventDispatcher<TESQuestStartStopEvent>*) override;
    BSTEventResult OnEvent(const TESQuestStageEvent*, const EventDispatcher<TESQuestStageEvent>*) override;

    void OnQuestUpdate(const NotifyQuestUpdate&) noexcept;
    void ApplyQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept;
    void RememberPartyQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept;
    void NotifyOverlayOfQuestUpdate(uint32_t aFormId) noexcept;

    World& m_world;

    static constexpr size_t kMaxCachedPartyQuestUpdates = 64;
    Vector<NotifyQuestUpdate> m_partyQuestUpdateCache;

    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_leftConnection;
    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_partyLeftConnection;
};
