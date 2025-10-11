namespace logger = SKSE::log;

namespace
{
    void OnPostLoadGame()
    {
        logger::info("Creating Event Sink(s)");
        try
        {
            Globals::g_actor_states.clear();
            const auto player = RE::PlayerCharacter::GetSingleton();
            player->RemoveAnimationGraphEventSink(Events::AttackAnimationGraphEventSink::GetSingleton());
            auto &state = Globals::g_actor_states[player->GetFormID()];
            if (!state.has_event_sink)
            {
                logger::info("Creating Player Event Sink");
                player->AddAnimationGraphEventSink(Events::AttackAnimationGraphEventSink::GetSingleton());
                state.has_event_sink = true;
            }
            else
                logger::info("Player already has Event Sink");
            if (!state.ledge_blocker && Globals::physical_blocker)
            {
                Objects::CreateLedgeBlocker(player);
            }
            if (state.ray_markers.empty() && Globals::show_markers)
                Objects::InitializeRayMarkers(player);
            if (Globals::enable_for_npcs)
                RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(Events::CombatEventSink::GetSingleton());
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

        Config::SetUpLog();
        logger::info("Animation Ledge Block NG Plugin Starting");
        Config::LoadConfig();
        Config::SetLogLevel();

        SKSE::GetMessagingInterface()->RegisterListener("SKSE", MessageHandler);

        logger::info("Animation Ledge Block NG Plugin Loaded");

        return true;
    }
}