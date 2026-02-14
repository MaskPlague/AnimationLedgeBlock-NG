namespace Utils
{
    int toggleSpellID = 0x804;
    int deactivatorSpellID = 0x805;
    const char *pluginName = "Animation Ledge Block NG.esp";

    void AddTogglePowerToPlayer()
    {
        RE::TESDataHandler *handler = RE::TESDataHandler::GetSingleton();
        RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
        if (!handler || !player)
            return;
        RE::SpellItem *togglePower = handler->LookupForm<RE::SpellItem>(toggleSpellID, pluginName);
        if (!togglePower)
            return;
        if (!player->HasSpell(togglePower))
            player->AddSpell(togglePower);
    }

    void RemoveSpellsFromPlayer()
    {
        RE::TESDataHandler *handler = RE::TESDataHandler::GetSingleton();
        RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
        if (!handler || !player)
            return;
        RE::SpellItem *togglePower = handler->LookupForm<RE::SpellItem>(toggleSpellID, pluginName);
        if (!togglePower)
            return;
        if (player->HasSpell(togglePower))
            player->RemoveSpell(togglePower);
        RE::SpellItem *deactivatorSpell = handler->LookupForm<RE::SpellItem>(deactivatorSpellID, pluginName);
        if (!deactivatorSpell)
            return;
        if (player->HasSpell(deactivatorSpell))
            player->RemoveSpell(deactivatorSpell);
    }

    bool PlayerHasDeactivatorSpell()
    {
        RE::TESDataHandler *handler = RE::TESDataHandler::GetSingleton();
        RE::PlayerCharacter *player = RE::PlayerCharacter::GetSingleton();
        if (!handler || !player)
            return false;
        RE::SpellItem *deactivatorSpell = handler->LookupForm<RE::SpellItem>(deactivatorSpellID, pluginName);
        if (!deactivatorSpell)
            return false;
        if (player->HasSpell(deactivatorSpell))
            return true;
        else
            return false;
    }

    void CleanupActors()
    {
        for (auto it = Globals::g_actor_states.begin(); it != Globals::g_actor_states.end();)
        {
            if (const auto actor = RE::TESForm::LookupByID<RE::Actor>(it->first); !actor || actor->IsDead() || actor->IsDeleted() || !actor->IsInCombat() || actor->IsDisabled())
            {
                if (actor->IsPlayerRef())
                {
                    ++it;
                    continue;
                }
                actor->RemoveAnimationGraphEventSink(Events::AttackAnimationGraphEventSink::GetSingleton());
                it = Globals::g_actor_states.erase(it);
            }
            else
                ++it;
        }
    }

    // Force the actor to stop moving toward their original vector
    void StopActorVelocity(RE::Actor *actor, Globals::ActorState &state)
    {
        Globals::ActorState *stateCheck = Globals::CheckState(actor);
        if (!stateCheck || !actor)
            return;
        auto *controller = actor->GetCharController();
        if (!controller)
        {
            logger::warn("Could not get actor character controller"sv);
            return;
        }

        controller->SetLinearVelocityImpl({0.0f, 0.0f, 0.0f, 0.0f});
        if (!Globals::physical_blocker)
        {
            if (Globals::log_level > 2)
                logger::trace("Teleportation blocking {}"sv, actor->GetName());
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
            const RE::NiPoint3 pos = actor->GetPosition();
            const RE::NiPoint3 dir_vec(std::sin(state.best_yaw), std::cos(state.best_yaw), 0.0f);
            const RE::NiPoint3 back_pos = pos - (dir_vec * 4.0f);
            actor->SetPosition(back_pos, true);
        }
    }

    bool IsLedgeAhead(RE::Actor *actor, Globals::ActorState &state)
    {
        Globals::ActorState *state_check = Globals::CheckState(actor);
        if (!state_check)
        {
            logger::debug("Actor state no longer exists, cancel ledge check."sv);
            return false;
        }

        if (!actor || actor->AsActorState()->IsSwimming() || actor->AsActorState()->IsFlying())
        {
            logger::warn("Either could not get actor or actor is swimming or on dragon."sv);
            return false;
        }
        auto char_controller = actor->GetCharController();
        if (char_controller && Globals::disable_on_stairs && char_controller->flags.any(RE::CHARACTER_FLAGS::kOnStairs))
        {
            logger::trace("Character on stairs and stairs disables ledge check."sv);
            return false;
        }

        const auto cell = actor->GetParentCell();
        if (!cell)
        {
            logger::warn("Ledge check couldn't get parent cell"sv);
            return false;
        }

        const auto bhk_world = cell->GetbhkWorld();
        if (!bhk_world)
        {
            logger::warn("Ledge check couldn't get bhkWorld"sv);
            return false;
        }

        const auto havok_world_scale = RE::bhkWorld::GetWorldScale();
        const float ray_length = 600.0f; // 600.0f

        const float direction_threshold = 0.7f; // Adjust for tighter/looser direction matching
        state.best_dist = -1.0f;
        RE::NiPoint3 actor_pos = actor->GetPosition();
        RE::NiPoint3 current_linear_velocity;
        actor->GetLinearVelocity(current_linear_velocity);
        current_linear_velocity.z = 0.0f; // z is not important
        float velocity_length = current_linear_velocity.Length();

        // Filter out actor collision from rays
        uint32_t collision_filter_info = 0;
        RE::CFilter collision_filter_info_cfilter;
        actor->GetCollisionFilterInfo(collision_filter_info_cfilter);
        collision_filter_info = collision_filter_info_cfilter.filter;
        uint32_t filter_info = (collision_filter_info & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kLOS);
        RE::NiPoint3 move_direction = {0.0f, 0.0f, 0.0f};
        if (velocity_length > 0.0f)
        {
            move_direction = current_linear_velocity / velocity_length;
        }
        // Yaw offsets to use for rays around the actor
        float actor_yaw = actor->GetAngleZ();
        std::vector<float> yaw_offsets;
        float angle_step = static_cast<float>(2.0 * RE::NI_PI / static_cast<long double>(Globals::num_rays));
        for (int i = 0; i < Globals::num_rays; ++i)
        {
            yaw_offsets.push_back(actor_yaw + i * angle_step);
        };
        int i = 0; // increment into ray markers
        bool ledge_detected = false;
        std::vector<float> valid_yaws;
        float dist_from_player = Globals::ledge_distance;
        bool al_flag1;
        bool al_flag2;
        bool opposite_dir;
        std::vector<float> hit_z;
        std::vector<float> op_hit_z;
        for (const float yaw : yaw_offsets)
        {
            dist_from_player = Globals::ledge_distance;
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
            ray.rayInput.filterInfo.filter = filter_info;

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

                if (Globals::show_markers) // if in debug mode move objects to ray hit positions
                {
                    if (auto marker = state.ray_markers[i]; marker)
                        marker->SetPosition(hit_pos.x, hit_pos.y, hit_pos.z + 20);
                    ++i;
                }
                if (opposite_dir)
                    op_hit_z.push_back(hit_pos.z);
                else
                    hit_z.push_back(hit_pos.z);
                if (actor_pos.z - hit_pos.z > Globals::drop_threshold)
                {
                    valid_yaws.push_back(yaw);
                }
            }
            else
            {
                if (opposite_dir)
                    op_hit_z.push_back(actor_pos.z - Globals::drop_threshold - 10);
                else
                    hit_z.push_back(actor_pos.z - Globals::drop_threshold - 10);
            }
        }
        if (!valid_yaws.empty())
        {
            float yaw = MathUtils::AverageAngles(valid_yaws);
            state.best_yaw = MathUtils::NormalizeAngle(yaw);
        }
        if (op_hit_z.empty())
            op_hit_z.push_back(actor_pos.z);
        if (!hit_z.empty())
        {
            ledge_detected = MathUtils::IsMaxMinZPastDropThreshold(hit_z, op_hit_z, actor->GetPositionZ());
        }
        ++state.loops;
        if (ledge_detected || state.loops > Globals::memory_duration)
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

    void EdgeCheck(RE::Actor *actor, Globals::ActorState &state)
    {
        Globals::ActorState *stateCheck = Globals::CheckState(actor);
        if (!stateCheck || !actor)
            return;
        {
            // logger::trace("Checking for ledge."sv);
            if (IsLedgeAhead(actor, state) && (state.is_attacking || state.is_on_ledge))
            {
                // logger::trace("Stopping actor velocity."sv);
                StopActorVelocity(actor, state);
            }
        }
        }
    }

    void CheckAllActorsForLedges()
    {
        for (auto actor_state : Globals::g_actor_states)
        {
            auto form_id = actor_state.first;
            auto actor_ptr = RE::TESForm::LookupByID<RE::Actor>(form_id);
            if (!actor_ptr)
                continue;
            auto &state = actor_state.second;
            if (state.is_jumping && (Globals::jump_duration > static_cast<float>(clock() - state.jump_start) / CLOCKS_PER_SEC))
                continue;
            else if (state.is_jumping)
                state.is_jumping = false;

            if (state.is_attacking || state.is_on_ledge)
            {
                EdgeCheck(actor_ptr, state);
            }
        }
    }
}