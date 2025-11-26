namespace Objects
{
    bool CreateLedgeBlocker(RE::Actor *actor)
    {
        auto *handler = RE::TESDataHandler::GetSingleton();
        if (!handler || !actor)
        {
            logger::warn("Error, could not get TESDataHandler"sv);
            return true;
        }
        RE::TESObjectSTAT *blocker;
        switch (Globals::physical_blocker_type)
        {
        case 0: // Half Ring wall
            blocker = handler->LookupForm<RE::TESObjectSTAT>(0x800, "Animation Ledge Block NG.esp");
            break;
        case 1: // Full Ring
            blocker = handler->LookupForm<RE::TESObjectSTAT>(0x801, "Animation Ledge Block NG.esp");
            break;
        case 2: // Shallow wall
            blocker = handler->LookupForm<RE::TESObjectSTAT>(0x802, "Animation Ledge Block NG.esp");
            break;
        default: // Default Half Ring wall
            blocker = handler->LookupForm<RE::TESObjectSTAT>(0x800, "Animation Ledge Block NG.esp");
        }
        logger::trace("using blocker type {}"sv, Globals::physical_blocker_type);
        if (!blocker)
        {
            logger::warn("Could not access Animation Ledge Block NG.esp"sv);
            return true;
        }
        auto placed = actor->PlaceObjectAtMe(blocker, true);
        placed->SetPosition(actor->GetPositionX(), actor->GetPositionY(), actor->GetPositionZ() - 10000);
        auto &state = Globals::GetState(actor);
        state.ledge_blocker = placed.get();
        return false;
    }

    // Call this once to spawn them near the actor
    void InitializeRayMarkers(RE::Actor *actor)
    {
        auto &state = Globals::GetState(actor);
        if (!state.ray_markers.empty())
        {
            return; // Already initialized
        }

        auto markerBase = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0004e4e6);
        // meridias beacon 0004e4e6
        // garnet 00063B45
        if (!markerBase || !actor)
        {
            return;
        }

        auto cell = actor->GetParentCell();
        if (!cell)
        {
            return;
        }

        // Spawn markers near actor and store references
        RE::NiPoint3 origin = actor->GetPosition();

        for (int i = 0; i < Globals::ray_marker_count; ++i)
        {
            auto placed = actor->PlaceObjectAtMe(markerBase, true);
            if (placed)
            {
                placed->SetPosition(actor->GetPositionX(), actor->GetPositionY(), actor->GetPositionZ() + 50);
                state.ray_markers.push_back(placed.get());
            }
        }
    }
}