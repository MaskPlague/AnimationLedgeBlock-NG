namespace Hook
{
    bool Install()
    {
        SKSE::AllocTrampoline(14);
        logger::info("Installing hooks, allocated 14 bytes to the trampoline."sv);

        auto *playerUpdateManager = PlayerUpdateListener::GetSingleton();
        if (!playerUpdateManager)
        {
            logger::critical("  >Failed to get manager singleton for the Player Update manager."sv);
            return false;
        }

        logger::info("  >Installing Player Update manager..."sv);
        bool installedPlayerUpdatePatch = playerUpdateManager->Install();

        return installedPlayerUpdatePatch;
    }

    bool PlayerUpdateListener::Install()
    {
        REL::Relocation<std::uintptr_t> VTABLE{RE::PlayerCharacter::VTABLE[0]};
        _func = VTABLE.write_vfunc(idx, Thunk);
        return true;
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
}
