#pragma once

namespace Globals
{
    extern bool show_markers;
    extern int log_level;
    extern bool teleport;
    extern float valid_safe_point_distance;
    extern bool enable_for_npcs;
    extern bool disable_on_stairs;
    extern bool enable_for_attacks;
    extern bool enable_for_dodges;
    extern bool enable_for_slides;
    extern bool use_spell_toggle;
    extern float drop_threshold;
    extern float ledge_distance;
    extern float ground_leeway;
    extern int memory_duration;
    extern float jump_duration;

    extern constexpr int num_rays = 12; // Number of rays to create.
    extern constexpr int ray_marker_count = num_rays * 2;

    struct ActorState
    {
        bool is_attacking = false;
        bool is_on_ledge = false;
        int loops = 0;
        bool is_looping = false;
        bool has_event_sink = false;

        float best_yaw = 0.0f;

        int animation_type = 0;

        int jump_start = 0;
        bool is_jumping = false;

        RE::TESObjectCELL *last_actor_cell;

        RE::TESObjectREFR *ledge_blocker;

        std::vector<RE::TESObjectREFR *> ray_markers;

        std::vector<RE::NiPoint3> safe_grounded_positions;
    };

    inline std::unordered_map<RE::FormID, ActorState> g_actor_states;

    ActorState &GetState(RE::Actor *actor);

    ActorState *CheckState(RE::Actor *actor);

}
