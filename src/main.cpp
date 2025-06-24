namespace logger = SKSE::log;

namespace
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

    constexpr int num_rays = 12; // Number of rays to create.
    constexpr int ray_marker_count = num_rays * 2;

    struct ActorState
    {
        bool is_attacking = false;
        bool is_on_ledge = false;
        int loops = 0;
        bool is_looping = false;
        bool moved_blocker = false;
        bool has_event_sink = false;

        float best_yaw = 0.0f;
        float best_dist = -1.0f;

        int until_move_again = 0;
        int until_moment_hide = 0;

        int after_attack_timer = 0;

        int animation_type = 0;

        int jump_start = 0;
        bool is_jumping = false;

        RE::TESObjectCELL *last_actor_cell;

        RE::TESObjectREFR *ledge_blocker;

        std::vector<RE::TESObjectREFR *> ray_markers;

        std::vector<RE::NiPoint3> safe_grounded_positions;
    };

    inline std::unordered_map<RE::FormID, ActorState> g_actor_states;

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

    void SetUpLog()
    {
        auto logs_folder = SKSE::log::log_directory();
        if (!logs_folder)
            SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
        auto plugin_name = SKSE::PluginDeclaration::GetSingleton()->GetName();
        auto log_file_path = *logs_folder / std::format("{}.log", plugin_name);
        auto file_logger_ptr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path.string(), true);
        auto logger_ptr = std::make_shared<spdlog::logger>("log", std::move(file_logger_ptr));
        spdlog::set_default_logger(std::move(logger_ptr));
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
    }

    void SetLogLevel()
    {
        switch (log_level)
        {
        case 0:
            spdlog::set_level(spdlog::level::err);
            break;
        case 1:
            spdlog::set_level(spdlog::level::warn);
            break;
        case 2:
            spdlog::set_level(spdlog::level::info);
            break;
        case 3:
            spdlog::set_level(spdlog::level::debug);
            break;
        case 4:
            spdlog::set_level(spdlog::level::trace);
            break;
        case 10:
            spdlog::set_level(spdlog::level::trace);
            show_markers = true;
            break;
        default:
            log_level = 2;
            spdlog::set_level(spdlog::level::info);
        }
    }

    void LoadConfig()
    {
        CSimpleIniA ini;
        ini.SetUnicode();

        SI_Error rc = ini.LoadFile("Data\\SKSE\\Plugins\\AnimationLedgeBlockNG.ini");
        if (rc < 0)
        {
            logger::warn("Could not load AnimationLedgeBlockNG.ini, using defaults");
        }

        // Read values:
        physical_blocker = ini.GetBoolValue("General", "PhysicalBlocker", true);
        physical_blocker_type = ini.GetLongValue("General", "PhysicalBlockerType", 0);
        disable_on_stairs = ini.GetBoolValue("General", "DisableOnStairs", true);
        enable_for_npcs = ini.GetBoolValue("General", "EnableNPCs", true);
        enable_for_attacks = ini.GetBoolValue("General", "EnableAttackBlocking", true);
        enable_for_dodges = ini.GetBoolValue("General", "EnableDodgeBlocking", true);
        enable_for_slides = ini.GetBoolValue("General", "EnableSlideBlocking", true);
        drop_threshold = static_cast<float>(ini.GetDoubleValue("Tweaks", "DropThreshold", 150.0f));
        if (drop_threshold > 600.0f)
            drop_threshold = 590.0f;
        ledge_distance = static_cast<float>(ini.GetDoubleValue("Tweaks", "LedgeDistance", 50.0f));
        if (physical_blocker && ledge_distance < 50.0f)
            ledge_distance = 50.0f;
        else if (!physical_blocker && ledge_distance < 10.0f)
            ledge_distance = 10.0f;
        ground_leeway = static_cast<float>(ini.GetDoubleValue("Tweaks", "GroundLeeway", 60.0f));
        jump_duration = static_cast<float>(ini.GetDoubleValue("Tweaks", "JumpDuration", jump_duration));
        memory_duration = ini.GetLongValue("Tweaks", "MemoryDuration", 10);
        memory_duration = std::max(memory_duration, 1);

        log_level = ini.GetLongValue("Debug", "LoggingLevel", 2);

        logger::debug("Version              {}", SKSE::PluginDeclaration::GetSingleton()->GetVersion());
        logger::debug("Physical Blocker:    {}", physical_blocker);
        logger::debug("PhysicalBlockerType: {}", physical_blocker_type);
        logger::debug("DisableOnStairs      {}", disable_on_stairs);
        logger::debug("EnableNPCs:          {}", enable_for_npcs);
        logger::debug("EnableAttackBlocking:{}", enable_for_attacks);
        logger::debug("EnableDodgeBlocking: {}", enable_for_dodges);
        logger::debug("EnableSlideBlocking: {}", enable_for_slides);
        logger::debug("DropThreshold:       {:.2f}", drop_threshold);
        logger::debug("LedgeDistance:       {:.2f}", ledge_distance);
        logger::debug("JumpDuration         {:.2f}", jump_duration);
        logger::debug("GroundLeeway         {:.2f}", ground_leeway);
        logger::debug("MemoryDuration:      {}", memory_duration);

        logger::debug("LoggingLevel:        {}", log_level);

        // Optionally write defaults back for any missing keys:
        ini.SetBoolValue("General", "PhysicalBlocker", physical_blocker);
        ini.SetLongValue("General", "PhysicalBlockerType", physical_blocker_type);
        ini.SetBoolValue("General", "DisableOnStairs", disable_on_stairs);
        ini.SetBoolValue("General", "EnableNPCs", enable_for_npcs);
        ini.SetBoolValue("General", "EnableAttackBlocking", enable_for_attacks);
        ini.SetBoolValue("General", "EnableDodgeBlocking", enable_for_dodges);
        ini.SetBoolValue("General", "EnableSlideBlocking", enable_for_slides);

        ini.SetDoubleValue("Tweaks", "DropThreshold", static_cast<double>(drop_threshold));
        ini.SetDoubleValue("Tweaks", "LedgeDistance", static_cast<double>(ledge_distance));
        ini.SetDoubleValue("Tweaks", "JumpDuration", jump_duration);
        ini.SetDoubleValue("Tweaks", "GroundLeeway", static_cast<double>(ground_leeway));
        ini.SetLongValue("Tweaks", "MemoryDuration", memory_duration);

        ini.SetLongValue("Debug", "LoggingLevel", log_level);
        ini.SaveFile("Data\\SKSE\\Plugins\\AnimationLedgeBlockNG.ini");
    }

    bool CreateLedgeBlocker(RE::Actor *actor)
    {
        auto *handler = RE::TESDataHandler::GetSingleton();
        if (!handler || !actor)
        {
            logger::warn("Error, could not get TESDataHandler");
            return true;
        }
        RE::TESObjectSTAT *blocker;
        switch (physical_blocker_type)
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
        logger::trace("using blocker type {}", physical_blocker_type);
        if (!blocker)
        {
            logger::warn("Could not access Animation Ledge Block NG.esp");
            return true;
        }
        auto placed = actor->PlaceObjectAtMe(blocker, true);
        placed->SetPosition(actor->GetPositionX(), actor->GetPositionY(), actor->GetPositionZ() - 10000);
        auto &state = GetState(actor);
        state.ledge_blocker = placed.get();
        return false;
    }

    // Call this once to spawn them near the actor
    void InitializeRayMarkers(RE::Actor *actor)
    {
        auto &state = GetState(actor);
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

        for (int i = 0; i < ray_marker_count; ++i)
        {
            auto placed = actor->PlaceObjectAtMe(markerBase, true);
            if (placed)
            {
                placed->SetPosition(actor->GetPositionX(), actor->GetPositionY(), actor->GetPositionZ() + 50);
                state.ray_markers.push_back(placed.get());
            }
        }
    }

    float AverageAngles(const std::vector<float> &angles)
    {
        float x = 0.0f;
        float y = 0.0f;

        for (const float angle : angles)
        {
            x += std::cos(angle);
            y += std::sin(angle);
        }

        // Handle empty vector
        if (x == 0.0f && y == 0.0f)
            return 0.0f;

        return std::atan2(y, x);
    }

    float NormalizeAngle(const float angle)
    {
        return std::fmod((angle + 2 * RE::NI_PI), (2 * RE::NI_PI));
    }

    bool IsMaxMinZPastDropThreshold(const std::vector<float> hitZ, const std::vector<float> opHitZ, float actor_z)
    {
        if (hitZ.empty() || opHitZ.empty())
            return false;
        float max = opHitZ[0];
        for (const float z : opHitZ)
        {
            max = std::max(z, max);
        }
        max = std::min(max, actor_z);
        if (max <= actor_z - ground_leeway)
            return false;
        float min = hitZ[0];
        for (const float z : hitZ)
        {
            min = std::min(min, z);
        }
        auto diff = max - min;
        logger::trace("Diff {}", diff);
        if (diff >= drop_threshold)
            return true;
        else
            return false;
    }

    bool IsLedgeAhead(RE::Actor *actor, ActorState &state)
    {
        ActorState *state_check = CheckState(actor);
        if (!state_check)
        {
            logger::debug("Actor state no longer exists, cancel ledge check.");
            return false;
        }

        if (!actor || actor->AsActorState()->IsSwimming() || actor->AsActorState()->IsFlying())
        {
            logger::warn("Either could not get actor or actor is swimming or on dragon.");
            return false;
        }
        auto char_controller = actor->GetCharController();
        if (char_controller && disable_on_stairs && char_controller->flags.any(RE::CHARACTER_FLAGS::kOnStairs))
        {
            logger::trace("Character on stairs and stairs disables ledge check.");
            return false;
        }

        const auto cell = actor->GetParentCell();
        if (!cell)
        {
            logger::warn("Ledge check couldn't get parent cell");
            return false;
        }

        const auto bhk_world = cell->GetbhkWorld();
        if (!bhk_world)
        {
            logger::warn("Ledge check couldn't get bhkWorld");
            return false;
        }

        const auto havok_world_scale = RE::bhkWorld::GetWorldScale();
        const float ray_length = 600.0f; // 600.0f

        // const float maxStepUpHeight = 50.0f;   // not sure if this is working but I'm afraid to touch it
        const float direction_threshold = 0.7f; // Adjust for tighter/looser direction matching
        state.best_dist = -1.0f;
        RE::NiPoint3 actor_pos = actor->GetPosition();
        RE::NiPoint3 current_linear_velocity;
        actor->GetLinearVelocity(current_linear_velocity);
        current_linear_velocity.z = 0.0f; // z is not important
        float velocity_length = current_linear_velocity.Length();

        // Filter out actor collision from rays
        uint32_t collision_filter_info = 0;
        actor->GetCollisionFilterInfo(collision_filter_info);
        uint32_t filter_info = (collision_filter_info & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kLOS);
        RE::NiPoint3 move_direction = {0.0f, 0.0f, 0.0f};
        if (velocity_length > 0.0f)
        {
            move_direction = current_linear_velocity / velocity_length;
        }
        // Yaw offsets to use for rays around the actor
        float actor_yaw = actor->GetAngleZ();
        std::vector<float> yaw_offsets;
        float angle_step = static_cast<float>(2.0 * RE::NI_PI / static_cast<long double>(num_rays));
        for (int i = 0; i < num_rays; ++i)
        {
            yaw_offsets.push_back(actor_yaw + i * angle_step);
        };
        int i = 0; // increment into ray markers
        bool ledge_detected = false;
        std::vector<float> valid_yaws;
        float dist_from_player = ledge_distance;
        bool al_flag1;
        bool al_flag2;
        bool opposite_dir;
        std::vector<float> hit_z;
        std::vector<float> op_hit_z;
        for (const float yaw : yaw_offsets)
        {
            dist_from_player = ledge_distance;
            opposite_dir = false;
            RE::NiPoint3 dir_vec(std::sin(yaw), std::cos(yaw), 0.0f);
            float dir_length = dir_vec.Length();
            if (dir_length == 0.0f)
            {
                continue;
            }
            RE::NiPoint3 normalized_dir = dir_vec / dir_length;

            // Skip if we're moving and this direction doesn't match our movement
            if (velocity_length > 0.0f)
            {
                float alignment = normalized_dir.Dot(move_direction);
                float op_alignment = -(normalized_dir).Dot(move_direction);
                al_flag1 = false;
                al_flag2 = false;
                if (alignment < direction_threshold)
                    al_flag1 = true;
                if (op_alignment < direction_threshold)
                    al_flag2 = true;
                else
                {
                    dist_from_player = 100.0f;
                    opposite_dir = true;
                }

                if (al_flag1 && al_flag2)
                    continue;
            }
            else
            {
                continue;
            }

            RE::NiPoint3 ray_from = actor_pos + (normalized_dir * dist_from_player) + RE::NiPoint3(0, 0, 80);
            RE::NiPoint3 ray_to = ray_from + RE::NiPoint3(0, 0, -ray_length);

            RE::bhkPickData ray;
            ray.rayInput.from = ray_from * havok_world_scale;
            ray.rayInput.to = ray_to * havok_world_scale;
            ray.rayInput.filterInfo = filter_info;

            if (bhk_world->PickObject(ray) && ray.rayOutput.HasHit())
            {
                auto hitOwner = ray.rayOutput.rootCollidable->GetOwner<RE::hkpRigidBody>();
                RE::NiPoint3 delta = ray_to - ray_from;
                RE::NiPoint3 hit_pos = ray_from + delta * ray.rayOutput.hitFraction;
                if (hitOwner)
                {
                    auto *user_data = hitOwner->GetUserData();
                    auto *hit_ref = user_data ? user_data->As<RE::TESObjectREFR>() : nullptr;
                    auto *hit_object = hit_ref ? hit_ref->GetBaseObject() : nullptr;

                    if (hit_object && (hit_object->Is(RE::FormType::Flora) || hit_object->Is(RE::FormType::Tree)))
                    {
                        auto hit_ref_pos = hit_ref->GetPosition();
                        if (hit_ref_pos.z < hit_pos.z)
                            hit_pos = hit_ref_pos;
                    }
                }

                if (show_markers) // if in debug mode move objects to ray hit positions
                {
                    if (auto marker = state.ray_markers[i]; marker)
                        marker->SetPosition(hit_pos.x, hit_pos.y, hit_pos.z + 20);
                    ++i;
                }
                if (opposite_dir)
                    op_hit_z.push_back(hit_pos.z);
                else
                    hit_z.push_back(hit_pos.z);
                if (actor_pos.z - hit_pos.z > drop_threshold)
                {
                    valid_yaws.push_back(yaw);
                }
            }
            else if (physical_blocker)
            {
                valid_yaws.push_back(yaw);
            }
            else
            {
                if (opposite_dir)
                    op_hit_z.push_back(actor_pos.z - drop_threshold - 10);
                else
                    hit_z.push_back(actor_pos.z - drop_threshold - 10);
            }
        }
        if (!valid_yaws.empty())
        {
            float yaw = AverageAngles(valid_yaws);
            state.best_yaw = NormalizeAngle(yaw);
        }
        if (op_hit_z.empty())
            op_hit_z.push_back(actor_pos.z);
        if (!hit_z.empty())
        {
            ledge_detected = IsMaxMinZPastDropThreshold(hit_z, op_hit_z, actor->GetPositionZ());
        }
        ++state.loops;
        if (ledge_detected || state.loops > memory_duration)
        {
            state.is_on_ledge = ledge_detected;
            state.loops = 0;
        }
        if (!ledge_detected && !actor->IsInMidair())
        {
            state.safe_grounded_positions.push_back(actor_pos);
        }
        return ledge_detected;
    }

    // Taken from Better Grabbing
    void SetAngle(RE::TESObjectREFR *ref, const RE::NiPoint3 &a_position)
    {
        if (!ref)
        {
            return;
        }
        using func_t = void(RE::TESObjectREFR *, const RE::NiPoint3 &);
        const REL::Relocation<func_t> func{RELOCATION_ID(19359, 19786)};
        return func(ref, a_position);
    }
    //----------------------------

    // Force the actor to stop moving toward their original vector
    void StopActorVelocity(RE::Actor *actor, ActorState &state)
    {
        ActorState *stateCheck = CheckState(actor);
        if (!stateCheck || !actor)
            return;
        auto *controller = actor->GetCharController();
        if (!controller)
        {
            logger::warn("Could not get actor character controller");
            return;
        }

        controller->SetLinearVelocityImpl({0.0f, 0.0f, 0.0f, 0.0f});
        if (!physical_blocker)
        {
            if (log_level > 2)
                logger::trace("Teleportation blocking {}", actor->GetName());
            bool teleported = false;
            if (!state.safe_grounded_positions.empty())
            {
                auto back_pos = state.safe_grounded_positions.back();
                auto actor_pos = actor->GetPosition();
                auto distance = std::sqrt(pow(back_pos.x - actor_pos.x, 2) + pow(back_pos.y - actor_pos.y, 2) + pow(back_pos.z - actor_pos.z, 2));
                if (distance <= 10.0f)
                {
                    actor->SetPosition(back_pos, true);
                    teleported = true;
                }
            }
            if (!teleported)
            {
                const RE::NiPoint3 pos = actor->GetPosition();
                const RE::NiPoint3 dir_vec(std::sin(state.best_yaw), std::cos(state.best_yaw), 0.0f);
                const RE::NiPoint3 back_pos = pos - (dir_vec * 4.0f);
                actor->SetPosition(back_pos, true);
            }
        }
        else
        {
            if (!state.moved_blocker)
            {
                logger::trace("Moving a physical blocker to {}", actor->GetName());
                auto actor_pos = actor->GetPosition();
                RE::NiPoint3 obj_dir(std::sin(state.best_yaw), std::cos(state.best_yaw), 0.0f);
                obj_dir.Unitize();
                RE::NiPoint3 obj_pos = actor_pos - (obj_dir * 10.0f);
                state.ledge_blocker->SetPosition(obj_pos.x, obj_pos.y, actor_pos.z + 60);
                state.moved_blocker = true;
                SetAngle(state.ledge_blocker, {0.0f, 0.0f, state.best_yaw});
                state.ledge_blocker->Update3DPosition(true);
            }
        }
    }
    void CellChangeCheck(RE::Actor *actor)
    {
        if (const ActorState *state_check = CheckState(actor); !state_check)
            return;
        auto &state = GetState(actor);
        if (!physical_blocker || !actor || !state.ledge_blocker || !state.ledge_blocker->GetParentCell())
            return;

        if (auto *current_cell = actor->GetParentCell(); current_cell != state.last_actor_cell)
        {
            state.last_actor_cell = current_cell;
            if (state.ledge_blocker)
            {
                state.ledge_blocker->SetParentCell(current_cell);
                state.ledge_blocker->SetPosition(actor->GetPositionX(), actor->GetPositionY(), actor->GetPositionZ() - 10000.0f);
            }
        }
    }

    void EdgeCheck(RE::Actor *actor)
    {
        if (const ActorState *state_check = CheckState(actor); !state_check)
            return;

        auto &state = GetState(actor);
        if (!physical_blocker)
        {
            // logger::trace("Checking for ledge.");
            if (IsLedgeAhead(actor, state) && (state.is_attacking || state.is_on_ledge))
            {
                // logger::trace("Stopping actor velocity.");
                StopActorVelocity(actor, state);
            }
        }
        else
        {
            if (IsLedgeAhead(actor, state) && state.is_attacking)
            {
                ++state.until_move_again;
                StopActorVelocity(actor, state);
            }
            else if (state.is_attacking)
                ++state.until_move_again;

            if (state.until_move_again > 50)
            {
                state.until_move_again = 0;
                state.moved_blocker = false;
                ++state.until_moment_hide;
            }
            if (state.until_moment_hide > 3)
            {
                state.until_moment_hide = 0;
                state.moved_blocker = false;
                state.ledge_blocker->SetPosition(state.ledge_blocker->GetPositionX(), state.ledge_blocker->GetPositionY(), -10000.0f);
            }
        }
    }

    bool IsGameWindowFocused()
    {
        const HWND foreground = ::GetForegroundWindow();
        DWORD foreground_pid = 0;
        ::GetWindowThreadProcessId(foreground, &foreground_pid);
        return foreground_pid == ::GetCurrentProcessId();
    }

    void LoopEdgeCheck(RE::Actor *actor)
    {
        if (!actor)
            return;
        logger::trace("Starting Edge check loop");
        std::thread([actor]()
                    {
                RE::FormID form_id = actor->GetFormID();
                while (true) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(11));
                    if (!IsGameWindowFocused())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        //logger::trace("Window is tabbed out, stalling loop");
                        continue;
                    }
                    auto it = g_actor_states.find(form_id);
                    if (it == g_actor_states.end()) {
                        //logger::trace("Actor state no longer exists, ending LoopEdgeCheck");
                        break;
                    }

                    if (const auto actor_ptr = RE::TESForm::LookupByID<RE::Actor>(form_id); !actor_ptr) {
                        break;
                    }

                    auto& state = it->second;
                    if (state.is_jumping && (jump_duration > static_cast<float>(clock() - state.jump_start) / CLOCKS_PER_SEC)) {
                        //logger::trace("Jumping for {}", (float)(clock() - state.jump_start) / CLOCKS_PER_SEC);
                        continue;
                    }
                    else if (state.is_jumping)
                        state.is_jumping = false;

                    if (state.is_attacking || state.is_on_ledge)
                    {
                        if (!state.is_attacking && state.is_on_ledge)
                            ++state.after_attack_timer;
                        if (!state.is_attacking && state.is_on_ledge && state.after_attack_timer > 25)
                        {
                            state.after_attack_timer = 0;
                            break;
                        }
                        SKSE::GetTaskInterface()->AddTask([actor]()
                            {
                                CellChangeCheck(actor);
                                EdgeCheck(actor);
                            });
                    }
                    else {
                        break;
                    }
                } })
            .detach();
    }

    class AttackAnimationGraphEventSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent *event, RE::BSTEventSource<RE::BSAnimationGraphEvent> *)
        {
            if (!event || !event->holder)
            {
                return RE::BSEventNotifyControl::kStop;
            }
            // Cast away constness
            auto refr = const_cast<RE::TESObjectREFR *>(event->holder);
            auto actor = refr->As<RE::Actor>();
            if (!actor)
            {
                return RE::BSEventNotifyControl::kContinue;
            }
            auto holder_name = event->holder->GetName();
            ActorState *stateCheck = CheckState(actor);
            if (!stateCheck)
                return RE::BSEventNotifyControl::kContinue;
            auto &state = GetState(actor);
            logger::trace("{} Payload: {}", holder_name, event->payload);
            logger::trace("{} Tag: {}", holder_name, event->tag);
            if ((enable_for_attacks && event->tag == "PowerAttack_Start_end") ||
                (enable_for_dodges && (event->tag == "MCO_DodgeInitiate" ||
                                       event->tag == "RollTrigger" || event->tag == "SidestepTrigger" ||
                                       event->tag == "TKDR_DodgeStart" || event->tag == "MCO_DisableSecondDodge")) ||
                (enable_for_slides && event->tag == "SlideStart"))
            {
                state.is_attacking = true;
                logger::debug("Animation Started for {}", holder_name);
                if (!state.is_looping)
                    LoopEdgeCheck(actor);
                state.is_looping = true;
                state.until_move_again = 0;
                state.until_moment_hide = 0;
                state.after_attack_timer = 0;
                state.safe_grounded_positions.clear();
                if (event->tag == "PowerAttack_Start_end") // Any Attack
                    state.animation_type = 1;
                else if (event->tag == "MCO_DodgeInitiate") // DMCO
                    state.animation_type = 2;
                else if (event->tag == "RollTrigger" || event->tag == "SidestepTrigger") // TUDMR
                    state.animation_type = 3;
                else if (event->tag == "TKDR_DodgeStart") // TK Dodge RE
                    state.animation_type = 4;
                else if (event->tag == "MCO_DisableSecondDodge") // Old DMCO
                    state.animation_type = 5;
                else if (event->tag == "SlideStart") // Crouch Sliding
                    state.animation_type = 6;
            }
            else if (state.is_attacking &&
                     ((state.animation_type == 1 && event->tag == "attackStop") ||
                      (state.animation_type == 2 && event->payload == "$DMCO_Reset") ||
                      (state.animation_type == 3 && event->tag == "RollStop") ||
                      (state.animation_type == 4 && event->tag == "TKDR_DodgeEnd") ||
                      (state.animation_type == 5 && event->tag == "EnableBumper") ||
                      (state.animation_type == 6 && event->tag == "SlideStop") ||
                      state.animation_type == 0 ||
                      (state.animation_type != 1 && state.animation_type != 4 && event->tag == "InterruptCast") ||
                      (state.animation_type != 4 && event->tag == "IdleStop") ||
                      event->tag == "JumpUp" || event->tag == "MTstate"))
            {
                if (state.animation_type == 0)
                    logger::debug("Force ending LoopEdgeCheck");
                state.animation_type = 0;
                state.is_attacking = false;
                state.is_looping = false;
                state.moved_blocker = false;
                state.safe_grounded_positions.clear();
                if (physical_blocker)
                    state.ledge_blocker->SetPosition(state.ledge_blocker->GetPositionX(), state.ledge_blocker->GetPositionY(), -10000.0f);
                logger::debug("Animation Finished for {}", holder_name);
            }
            else if (!state.is_attacking && event->tag == "JumpUp")
            {
                state.jump_start = clock();
                state.is_jumping = true;
            }

            return RE::BSEventNotifyControl::kContinue;
        }

        static AttackAnimationGraphEventSink *GetSingleton()
        {
            static AttackAnimationGraphEventSink singleton;
            return &singleton;
        }
    };

    void CleanupActors()
    {
        for (auto it = g_actor_states.begin(); it != g_actor_states.end();)
        {
            if (const auto actor = RE::TESForm::LookupByID<RE::Actor>(it->first); !actor || actor->IsDead() || actor->IsDeleted() || !actor->IsInCombat() || actor->IsDisabled())
            {
                if (actor->IsPlayerRef())
                {
                    ++it;
                    continue;
                }
                actor->RemoveAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
                it = g_actor_states.erase(it);
            }
            else
                ++it;
        }
    }

    class CombatEventSink final : public RE::BSTEventSink<RE::TESCombatEvent>
    {
    public:
        virtual RE::BSEventNotifyControl ProcessEvent(
            const RE::TESCombatEvent *a_event,
            RE::BSTEventSource<RE::TESCombatEvent> *) override
        {
            if (!a_event || !a_event->actor)
            {
                return RE::BSEventNotifyControl::kContinue;
            }
            auto actor = a_event->actor->As<RE::Actor>();
            if (!actor || actor->IsPlayerRef() || !actor->GetActorBase() || !actor->GetActorBase()->GetRace())
                return RE::BSEventNotifyControl::kContinue;
            auto race = actor->GetActorBase()->GetRace();
            if (!race->HasKeywordString("ActorTypeNPC"))
            {
                return RE::BSEventNotifyControl::kContinue;
            }
            auto formID = actor->GetFormID();
            auto combatState = a_event->newState;
            if (combatState == RE::ACTOR_COMBAT_STATE::kCombat && !g_actor_states.contains(formID))
            {
                auto &state = g_actor_states[formID];
                if (!state.ledge_blocker && physical_blocker)
                {
                    CreateLedgeBlocker(actor);
                }
                if (state.ray_markers.empty() && show_markers)
                    InitializeRayMarkers(actor);
                actor->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
                logger::debug("Tracking new combat actor: {}", actor->GetName());
            }
            else if (combatState == RE::ACTOR_COMBAT_STATE::kNone && g_actor_states.contains(formID))
            {
                auto it = g_actor_states.find(formID);
                if (it != g_actor_states.end())
                {
                    if (it->second.ledge_blocker)
                    {
                        it->second.ledge_blocker->Disable();
                        it->second.ledge_blocker->SetPosition(
                            it->second.ledge_blocker->GetPositionX(),
                            it->second.ledge_blocker->GetPositionY(),
                            -10000.0f);
                    }
                    g_actor_states.erase(it);
                    actor->RemoveAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
                    logger::debug("Stopped tracking actor: {}", actor->GetName());
                }
            }
            CleanupActors();
            return RE::BSEventNotifyControl::kContinue;
        }
        static CombatEventSink *GetSingleton()
        {
            static CombatEventSink singleton;
            return &singleton;
        }
    };

    void OnPostLoadGame()
    {
        logger::info("Creating Event Sink(s)");
        try
        {
            g_actor_states.clear();
            const auto player = RE::PlayerCharacter::GetSingleton();
            RE::BSAnimationGraphManagerPtr manager;
            player->RemoveAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
            auto &state = g_actor_states[player->GetFormID()];
            if (!state.has_event_sink)
            {
                logger::info("Creating Player Event Sink");
                player->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
                state.has_event_sink = true;
            }
            else
                logger::info("Player already has Event Sink");
            if (!state.ledge_blocker && physical_blocker)
            {
                CreateLedgeBlocker(player);
            }
            if (state.ray_markers.empty() && show_markers)
                InitializeRayMarkers(player);
            if (enable_for_npcs)
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(CombatEventSink::GetSingleton());
            logger::info("Event Sink(s) Created");
        }
        catch (...)
        {
            logger::error("Failed to Create Event Sink(s)");
        }
    }

    void MessageHandler(SKSE::MessagingInterface::Message *msg)
    {
        if (msg->type != SKSE::MessagingInterface::kPostLoadGame)
            return;
        logger::debug("Received PostLoadGame message");
        if (!bool(msg->data))
        {
            logger::debug("PostLoadGame: false");
            return;
        }
        logger::debug("PostLoadGame: true");
        OnPostLoadGame();
    }

    extern "C" DLLEXPORT bool SKSEPlugin_Load(const SKSE::LoadInterface *skse)
    {
        SKSE::Init(skse);

        SetUpLog();
        logger::info("Animation Ledge Block NG Plugin Starting");
        LoadConfig();
        SetLogLevel();

        SKSE::GetMessagingInterface()->RegisterListener("SKSE", MessageHandler);

        logger::info("Animation Ledge Block NG Plugin Loaded");

        return true;
    }
}