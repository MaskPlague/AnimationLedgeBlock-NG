namespace Globals
{
    bool show_markers = false;
    int log_level = 2;
    bool teleport = true;
    float valid_safe_point_distance = 10.0f;
    bool enable_for_npcs = true;
    bool disable_on_stairs = true;
    bool enable_for_attacks = true;
    bool enable_for_dodges = true;
    bool enable_for_slides = true;
    bool use_spell_toggle = false;
    float drop_threshold = 150.0f; // 1.5x 1.0 player height
    float ledge_distance = 25.0f;  // 25.0 units around the player
    float ground_leeway = 90.0f;
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