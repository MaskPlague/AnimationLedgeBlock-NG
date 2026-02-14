#pragma once

namespace Utils
{
    void AddTogglePowerToPlayer();

    void RemoveSpellsFromPlayer();

    bool PlayerHasDeactivatorSpell();

    void CleanupActors();

    bool IsLedgeAhead(RE::Actor *actor, Globals::ActorState &state);

    void EdgeCheck(RE::Actor *actor, Globals::ActorState &state);

    void CheckAllActorsForLedges();
}