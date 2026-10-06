#pragma once

#include <World.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Messages/NotifyQuestUpdate.h>

#include <chrono>
#include <mutex>

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

    // Resync echo guard: game quest events caused by ReapplyCachedPartyQuestUpdates() must not be
    // re-emitted as RequestQuestUpdate (cached updates can be older than the server's 30s dedup).
    // The events may fire synchronously or later on another thread (thread_local ScopedQuestOverride
    // misses those), so expected echoes are recorded per quest (formId + stage), consumed once by
    // the matching OnEvent, and expire after kResyncEchoWindow.
    static constexpr uint8_t kEchoNone = 0;
    static constexpr uint8_t kEchoStarted = 1 << 0;
    static constexpr uint8_t kEchoStopped = 1 << 1;
    struct ResyncEcho
    {
        uint32_t FormId;
        uint16_t Stage;
        bool StageEcho;
        uint8_t StartStopMask; // kEchoStarted / kEchoStopped
        std::chrono::steady_clock::time_point Expiry;
    };
    static constexpr std::chrono::milliseconds kResyncEchoWindow{5000};
    void ExpectResyncEcho(uint32_t aFormId, uint16_t aStage, bool aStageEcho, uint8_t aStartStopMask) noexcept;
    bool ConsumeResyncStageEcho(uint32_t aFormId, uint16_t aStage) noexcept;
    bool ConsumeResyncStartStopEcho(uint32_t aFormId, bool aStarted) noexcept;
    bool ConsumeResyncEcho(uint32_t aFormId, uint16_t aStage, bool aIsStageEvent, bool aStarted) noexcept;

    bool m_isResyncing{false};
    std::mutex m_resyncEchoMutex;
    Vector<ResyncEcho> m_resyncEchoes;

    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_leftConnection;
    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_questSceneUpdateConnection;
};
