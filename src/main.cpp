namespace
{
    void OnPostLoadGame()
    {
        logger::info("Creating Event Sink(s)"sv);
        try
        {
            Globals::g_actor_states.clear();
            const auto player = RE::PlayerCharacter::GetSingleton();
            player->RemoveAnimationGraphEventSink(Events::AttackAnimationGraphEventSink::GetSingleton());
            logger::info("Creating Player Event Sink"sv);
            player->AddAnimationGraphEventSink(Events::AttackAnimationGraphEventSink::GetSingleton());

            auto &state = Globals::g_actor_states[player->GetFormID()];

            if (state.ray_markers.empty() && Globals::show_markers)
                Objects::InitializeRayMarkers(player);
            if (Globals::enable_for_npcs)
            {
                logger::info("Creating Combat Event Sink"sv);
                RE::ScriptEventSourceHolder::GetSingleton()->RemoveEventSink(Events::CombatEventSink::GetSingleton());
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(Events::CombatEventSink::GetSingleton());
            }
            if (Globals::use_spell_toggle)
                Utils::AddTogglePowerToPlayer();
            else
                Utils::RemoveSpellsFromPlayer();

            logger::info("Event Sink(s) Created"sv);
        }
        catch (...)
        {
            logger::error("Failed to Create Event Sink(s)"sv);
        }
    }

    void MessageHandler(SKSE::MessagingInterface::Message *msg)
    {
        if (msg->type != SKSE::MessagingInterface::kPostLoadGame)
            return;
        logger::debug("Received PostLoadGame message"sv);
        if (!bool(msg->data))
        {
            logger::debug("PostLoadGame: false"sv);
            return;
        }
        logger::debug("PostLoadGame: true"sv);
        OnPostLoadGame();
    }

    extern "C" DLLEXPORT bool SKSEPlugin_Load(const SKSE::LoadInterface *skse)
    {
        SKSE::Init(skse);

        Config::SetUpLog();
        logger::info("Animation Ledge Block NG Plugin Starting"sv);
        Config::LoadConfig();
        Config::SetLogLevel();

        SKSE::GetMessagingInterface()->RegisterListener("SKSE", MessageHandler);
        if (Hook::Install())
            logger::info("  >Hook installed"sv);
        else
            SKSE::stl::report_and_fail("Failed to install necessary hook."sv);

        logger::info("Animation Ledge Block NG Plugin Loaded"sv);

        return true;
    }
}