namespace Globals
{
    bool show_markers = false;
    int log_level = 2;
    bool physical_blocker = true;
    bool enable_for_npcs = true;
    bool disable_on_stairs = true;
    bool enable_for_attacks = true;
    bool enable_for_dodges = true;
    bool enable_for_slides = true;
    float drop_threshold = 150.0f; // 1.5x 1.0 player height
    float ledge_distance = 50.0f;  // 50.0 units around the player
    float ground_leeway = 60.0f;
    int physical_blocker_type = 0;
    int memory_duration = 10;
    float jump_duration = 1.5f;

    ActorState &GetState(RE::Actor *actor)
    {
        return g_actor_states[actor->GetFormID()];
    }

    ActorState *CheckState(RE::Actor *actor)
    {
        if (!actor)
            return nullptr;
        auto it = g_actor_states.find(actor->GetFormID());
        return it != g_actor_states.end() ? &it->second : nullptr;
    }
}