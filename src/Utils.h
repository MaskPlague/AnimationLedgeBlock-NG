#pragma once

namespace Utils
{
    void CleanupActors();

    bool IsLedgeAhead(RE::Actor *actor, Globals::ActorState &state);

    void LoopEdgeCheck(RE::Actor *actor);
}