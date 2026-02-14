namespace Objects
{
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