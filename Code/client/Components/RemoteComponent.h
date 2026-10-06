#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/Inventory.h>

struct RemoteComponent
{
    RemoteComponent(uint32_t aId, uint32_t aRefId, uint32_t aOwnershipEpoch, uint32_t aOwnerPlayerId = 0) noexcept
        : Id(aId)
        , CachedRefId(aRefId)
        , OwnershipEpoch(aOwnershipEpoch)
        , OwnerPlayerId(aOwnerPlayerId)
    {
    }

    uint32_t Id;
    uint32_t CachedRefId;
    uint32_t OwnershipEpoch;
    // Last known remote owner player id (0 = unknown). Used to avoid cross-party claim spam.
    uint32_t OwnerPlayerId{0};
};
