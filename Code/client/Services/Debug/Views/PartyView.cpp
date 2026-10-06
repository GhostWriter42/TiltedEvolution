#include <Services/DebugService.h>
#include <Services/QuestService.h>
#include <Services/PartyService.h>

#include <Messages/PartyKickRequest.h>
#include <Messages/PartyChangeLeaderRequest.h>
#include <Messages/PartyInviteRequest.h>
#include <Messages/PartyAcceptInviteRequest.h>
#include <Messages/PartyLeaveRequest.h>
#include <Messages/PartyCreateRequest.h>
#include <Messages/TeleportCommandRequest.h>
#include <Messages/RequestCurrentWeather.h>

#include <World.h>
#include <PlayerCharacter.h>
#include <Forms/TESQuest.h>

#include <imgui.h>

void DebugService::DrawPartyView()
{
    if (!m_transport.IsConnected())
        return;

    ImGui::Begin("Party");

    auto& partyService = m_world.GetPartyService();
    auto& players = partyService.GetPlayers();
    auto& members = partyService.GetPartyMembers();
    auto& invitations = partyService.GetInvitations();

    if (partyService.IsInParty())
    {
        ImGui::Text("Party Members");
        String pName{"You"};
        if (partyService.IsLeader())
        {
            pName += " (Leader)";
        }
        ImGui::BulletText(pName.c_str());

        for (auto& playerId : members)
        {
            ImGui::PushID(playerId);

            auto playerEntry = players.find(playerId);
            if (playerEntry != players.end())
            {
                auto playerName = playerEntry.value();
                if (playerId == partyService.GetLeaderPlayerId())
                {
                    playerName += " (Leader)";
                }
                ImGui::BulletText(playerName.c_str());

                ImGui::SameLine(200);
                if (ImGui::Button("Teleport"))
                {
                    TeleportCommandRequest request{};
                    request.TargetPlayer = playerEntry.value();

                    m_transport.Send(request);
                }

                if (partyService.IsLeader())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Kick"))
                    {
                        PartyKickRequest kickMessage;
                        kickMessage.PartyMemberPlayerId = playerEntry.key();
                        m_transport.Send(kickMessage);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Make Leader"))
                    {
                        PartyChangeLeaderRequest changeMessage;
                        changeMessage.PartyMemberPlayerId = playerEntry.key();
                        m_transport.Send(changeMessage);
                    }
                }
            }

            ImGui::PopID();
        }

        if (partyService.IsLeader())
        {
            ImGui::NewLine();
            ImGui::Text("Other Players");
            auto playerCount = 0;
            for (auto& player : partyService.GetPlayers())
            {
                if (std::find(std::begin(members), std::end(members), player.first) != std::end(members))
                    continue;

                playerCount++;
                ImGui::BulletText(player.second.c_str());
                ImGui::SameLine(100);
                if (ImGui::Button("Invite"))
                {
                    PartyInviteRequest request;
                    request.PlayerId = player.first;
                    m_transport.Send(request);
                }
            }

            if (playerCount == 0)
            {
                ImGui::BulletText("<No one online>");
            }
        }
    }

    for (auto& player : players)
    {
        auto itor = invitations.find(player.first);
        if (itor != std::end(invitations))
        {
            if (std::find(std::begin(members), std::end(members), player.first) != std::end(members))
                continue;

            ImGui::Text(player.second.c_str());
            ImGui::SameLine(100);
            if (ImGui::Button("Accept"))
            {
                invitations.erase(itor);

                PartyAcceptInviteRequest message;
                message.InviterId = player.first;
                m_transport.Send(message);
            }
        }
    }

    ImGui::NewLine();
    if (partyService.IsInParty())
    {
        if (ImGui::Button("Leave"))
        {
            PartyLeaveRequest request;
            m_transport.Send(request);
        }
    }
    else
    {
        if (ImGui::Button("Create Party"))
        {
            PartyCreateRequest request;
            m_transport.Send(request);
        }
    }

    if (partyService.IsInParty())
    {
        ImGui::Separator();
        ImGui::Text("Guest desync recovery");
        ImGui::TextWrapped(
            "Party-safe tools for members who fell behind. Does not pull the leader's quest log "
            "(no new network opcodes). Re-applies NotifyQuestUpdate already received, refreshes "
            "party weather, and teleports to the leader. Cannot fix updates you never received, "
            "scene/dialogue stuck states (#854/#848), or quest-item aliases.");

        auto& questService = m_world.ctx().at<QuestService>();
        const auto& cache = questService.GetCachedPartyQuestUpdates();
        ImGui::Text("Cached party quest updates: %zu", cache.size());

        if (cache.empty())
        {
            ImGui::TextDisabled("Re-apply unavailable (cache empty).");
        }
        else if (ImGui::Button("Re-apply cached party quest updates"))
        {
            const size_t applied = questService.ReapplyCachedPartyQuestUpdates();
            spdlog::info("Guest recovery: reapplied {} cached quest update(s)", applied);
        }
        ImGui::SameLine();
        if (cache.empty())
            ImGui::TextDisabled("Clear unavailable.");
        else if (ImGui::Button("Clear quest update cache"))
            questService.ClearCachedPartyQuestUpdates();

        if (!partyService.IsLeader())
        {
            if (ImGui::Button("Refresh party weather"))
            {
                RequestCurrentWeather request{};
                m_transport.Send(request);
            }

            const uint32_t leaderId = partyService.GetLeaderPlayerId();
            auto leaderEntry = players.find(leaderId);
            if (leaderId != 0 && leaderId != static_cast<uint32_t>(-1) && leaderEntry != players.end())
            {
                ImGui::SameLine();
                if (ImGui::Button("Teleport to leader"))
                {
                    TeleportCommandRequest request{};
                    request.TargetPlayer = leaderEntry.value();
                    m_transport.Send(request);
                }
            }
            else
            {
                ImGui::TextDisabled("Teleport unavailable (no leader / name unknown).");
            }
        }
        else
        {
            ImGui::TextDisabled("Leader: teleport-to-self disabled; use Quests debugger for force-setStage.");
        }

        if (ImGui::CollapsingHeader("Local syncable quest stages (read-only)"))
        {
            auto* pPlayer = PlayerCharacter::Get();
            if (!pPlayer)
            {
                ImGui::TextDisabled("No local player.");
            }
            else
            {
                Set<uint32_t> foundQuests{};
                int shown = 0;
                for (auto& objective : pPlayer->objectives)
                {
                    TESQuest* pQuest = objective.instance ? objective.instance->quest : nullptr;
                    if (!pQuest || QuestService::IsNonSyncableQuest(pQuest) || !pQuest->IsActive())
                        continue;
                    if (foundQuests.contains(pQuest->formID))
                        continue;
                    foundQuests.insert(pQuest->formID);
                    ImGui::BulletText("%s (%s) stage %u", pQuest->fullName.value.AsAscii(), pQuest->idName.AsAscii(),
                                      static_cast<unsigned>(pQuest->currentStage));
                    ++shown;
                }
                if (shown == 0)
                    ImGui::TextDisabled("No active syncable quests on this client.");
            }
        }

        if (!cache.empty() && ImGui::CollapsingHeader("Cached NotifyQuestUpdate list"))
        {
            for (size_t i = 0; i < cache.size(); ++i)
            {
                const auto& u = cache[i];
                const char* status = "StageUpdate";
                if (u.Status == NotifyQuestUpdate::Started)
                    status = "Started";
                else if (u.Status == NotifyQuestUpdate::Stopped)
                    status = "Stopped";
                ImGui::BulletText("#%zu id %08X:%08X stage %u %s", i, u.Id.ModId, u.Id.BaseId,
                                  static_cast<unsigned>(u.Stage), status);
            }
        }
    }


    ImGui::End();
}

