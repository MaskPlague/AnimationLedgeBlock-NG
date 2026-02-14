namespace Events
{
    RE::BSEventNotifyControl CombatEventSink::ProcessEvent(
        const RE::TESCombatEvent *a_event,
        RE::BSTEventSource<RE::TESCombatEvent> *)
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
        if (combatState == RE::ACTOR_COMBAT_STATE::kCombat && !Globals::g_actor_states.contains(formID))
        {
            auto &state = Globals::g_actor_states[formID];
            if (state.ray_markers.empty() && Globals::show_markers)
                Objects::InitializeRayMarkers(actor);
            actor->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
            logger::debug("Tracking new combat actor: {}"sv, actor->GetName());
        }
        else if (combatState == RE::ACTOR_COMBAT_STATE::kNone && Globals::g_actor_states.contains(formID))
        {
            auto it = Globals::g_actor_states.find(formID);
            if (it != Globals::g_actor_states.end())
            {
                if (it->second.ledge_blocker)
                {
                    it->second.ledge_blocker->Disable();
                    it->second.ledge_blocker->SetPosition(
                        it->second.ledge_blocker->GetPositionX(),
                        it->second.ledge_blocker->GetPositionY(),
                        -10000.0f);
                }
                Globals::g_actor_states.erase(it);
                actor->RemoveAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
                logger::debug("Stopped tracking actor: {}"sv, actor->GetName());
            }
        }
        Utils::CleanupActors();
        return RE::BSEventNotifyControl::kContinue;
    }
    CombatEventSink *CombatEventSink::GetSingleton()
    {
        static CombatEventSink singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl AttackAnimationGraphEventSink::ProcessEvent(
        const RE::BSAnimationGraphEvent *event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent> *)
    {
        if (!event || !event->holder || !event->holder->data.objectReference)
            return RE::BSEventNotifyControl::kContinue;

        // Cast away constness
        auto refr = const_cast<RE::TESObjectREFR *>(event->holder);
        auto actor = refr ? refr->As<RE::Actor>() : nullptr;
        if (!actor)
            return RE::BSEventNotifyControl::kContinue;

        const char *holder_name = event->holder->GetName();
        Globals::ActorState *stateCheck = Globals::CheckState(actor);
        if (!stateCheck)
            return RE::BSEventNotifyControl::kContinue;
        Globals::ActorState &state = Globals::GetState(actor);
        const RE::BSFixedString tag = event->tag;
        const RE::BSFixedString payload = event->payload;
        logger::trace("{} Payload: {}"sv, holder_name, payload.c_str());
        logger::trace("{} Tag: {}"sv, holder_name, tag.c_str());
        if ((Globals::enable_for_attacks && tag == "PowerAttack_Start_end") ||
            (Globals::enable_for_dodges && (tag == "MCO_DodgeInitiate" ||
                                            tag == "RollTrigger" || tag == "SidestepTrigger" ||
                                            tag == "TKDR_DodgeStart" || tag == "MCO_DisableSecondDodge")) ||
            (Globals::enable_for_slides && tag == "SlideStart"))
        {
            state.is_attacking = true;
            logger::debug("Animation Started for {}"sv, holder_name);
            state.safe_grounded_positions.clear();
            if (tag == "PowerAttack_Start_end") // Any Attack
                state.animation_type = 1;
            else if (tag == "MCO_DodgeInitiate") // DMCO
                state.animation_type = 2;
            else if (tag == "RollTrigger" || tag == "SidestepTrigger") // TUDMR
                state.animation_type = 3;
            else if (tag == "TKDR_DodgeStart") // TK Dodge RE
                state.animation_type = 4;
            else if (tag == "MCO_DisableSecondDodge") // Old DMCO
                state.animation_type = 5;
            else if (tag == "SlideStart") // Crouch Sliding
                state.animation_type = 6;
        }
        else if (state.is_attacking &&
                 ((state.animation_type == 1 && tag == "attackStop") ||
                  (state.animation_type == 2 && payload == "$DMCO_Reset") ||
                  (state.animation_type == 3 && tag == "RollStop") ||
                  (state.animation_type == 4 && tag == "TKDR_DodgeEnd") ||
                  (state.animation_type == 5 && tag == "EnableBumper") ||
                  (state.animation_type == 6 && tag == "SlideStop") ||
                  state.animation_type == 0 ||
                  (state.animation_type != 1 && state.animation_type != 4 && tag == "InterruptCast") ||
                  (state.animation_type != 4 && tag == "IdleStop") ||
                  tag == "JumpUp" || tag == "MTstate"))
        {
            state.animation_type = 0;
            state.is_attacking = false;
            state.is_on_ledge = false;
            logger::debug("Animation Finished for {}"sv, holder_name);
        }
        else if (!state.is_attacking && tag == "JumpUp")
        {
            state.jump_start = clock();
            state.is_jumping = true;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    AttackAnimationGraphEventSink *AttackAnimationGraphEventSink::GetSingleton()
    {
        static AttackAnimationGraphEventSink singleton;
        return &singleton;
    }

}
