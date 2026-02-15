namespace Hook
{
    bool Install()
    {
        SKSE::AllocTrampoline(14);
        logger::info("Installing hook, allocated 14 bytes to the trampoline."sv);

        auto *playerUpdateManager = PlayerUpdateListener::GetSingleton();
        if (!playerUpdateManager)
        {
            logger::critical("  >Failed to get manager singleton for the Player Update manager."sv);
            return false;
        }

        logger::info("  >Installing Player Update manager..."sv);
        playerUpdateManager->Install();
        logger::info("  >Installing Motion Update Hook..."sv);
        MotionUpdateHook::InstallHook();
        return true;
    }

    void PlayerUpdateListener::Install()
    {
        REL::Relocation<std::uintptr_t> VTABLE{RE::PlayerCharacter::VTABLE[0]};
        _func = VTABLE.write_vfunc(idx, Thunk);
        return;
    }

    inline void PlayerUpdateListener::Thunk(RE::PlayerCharacter *a_this, float a_delta)
    {
        _func(a_this, a_delta);
        internalCounter += std::max(0.0f, a_delta);
        internalCleanCounter += std::max(0.0f, a_delta);
        if (internalCounter >= timeBetweenChecks)
        {
            internalCounter = 0.0f;
            if (!Globals::use_spell_toggle || !Utils::PlayerHasDeactivatorSpell())
                Utils::CheckAllActorsForLedges();
        }
        if (internalCleanCounter >= timeBetweenCleaning)
        {
            internalCleanCounter = 0.0f;
            Utils::CleanupActors();
        }
        internalCounter = std::clamp(internalCounter, 0.0f, timeBetweenChecks);
        internalCleanCounter = std::clamp(internalCleanCounter, 0.0f, timeBetweenCleaning);
    }

    // target and offset from https://github.com/VanCZ1/Block-Cancel-Fix/blob/main/src/Hooks.cpp
    void MotionUpdateHook::InstallHook()
    {
        // 1.5.97: sub_1407020E0 call sub_1404E6360
        // 1.6.1170: sub_140797ED0 call sub_140540990
        // MovementTweenerAgentAnimationDriven::VTABLE[0], offset 0x11
        // call Character::ProcessMotionData

        REL::RelocationID target{41160, 42246};
        size_t offset = REL::VariantOffset{0x111, 0xFF, 0x111}.offset();

        auto &trampoline = SKSE::GetTrampoline();
        _func = trampoline.write_call<5>(target.address() + offset, Thunk);
        return;
    }

    bool MotionUpdateHook::Thunk(RE::Character *a_character, float a_deltaTime, RE::NiPoint3 *a_translation, RE::NiPoint3 *a_rotation, bool *a_result)
    {
        auto result = _func(a_character, a_deltaTime, a_translation, a_rotation, a_result);

        Globals::ActorState *stateCheck = Globals::CheckState(a_character);

        if (stateCheck)
        {
            Globals::ActorState &state = Globals::GetState(a_character);
            if (state.is_on_ledge)
            {
                a_translation->x = 0.0f;
                a_translation->y = 0.0f;
            }
        }

        return result;
    }
}
