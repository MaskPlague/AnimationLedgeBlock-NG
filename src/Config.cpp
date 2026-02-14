namespace Config
{
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
        switch (Globals::log_level)
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
            Globals::show_markers = true;
            break;
        default:
            Globals::log_level = 2;
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
            logger::warn("Could not load AnimationLedgeBlockNG.ini, using defaults"sv);
        }

        Globals::use_spell_toggle = ini.GetBoolValue("General", "UseTogglePower", false);
        Globals::teleport = ini.GetBoolValue("General", "Teleport", true);
        Globals::disable_on_stairs = ini.GetBoolValue("General", "DisableOnStairs", true);
        Globals::enable_for_npcs = ini.GetBoolValue("General", "EnableNPCs", true);
        Globals::enable_for_attacks = ini.GetBoolValue("General", "EnableAttackBlocking", true);
        Globals::enable_for_dodges = ini.GetBoolValue("General", "EnableDodgeBlocking", true);
        Globals::enable_for_slides = ini.GetBoolValue("General", "EnableSlideBlocking", true);
        Globals::drop_threshold = static_cast<float>(ini.GetDoubleValue("Tweaks", "DropThreshold", 150.0f));
        if (Globals::drop_threshold > 600.0f)
            Globals::drop_threshold = 590.0f;
        Globals::ledge_distance = static_cast<float>(ini.GetDoubleValue("Tweaks", "LedgeDistance", 25.0f));
        Globals::ground_leeway = static_cast<float>(ini.GetDoubleValue("Tweaks", "GroundLeeway", 90.0f));
        Globals::jump_duration = static_cast<float>(ini.GetDoubleValue("Tweaks", "JumpDuration", Globals::jump_duration));
        Globals::memory_duration = ini.GetLongValue("Tweaks", "MemoryDuration", 10);
        Globals::memory_duration = std::max(Globals::memory_duration, 1);

        Globals::log_level = ini.GetLongValue("Debug", "LoggingLevel", 2);

        logger::debug("Version              {}"sv, SKSE::PluginDeclaration::GetSingleton()->GetVersion());
        logger::debug("UseTogglePower:      {}"sv, Globals::use_spell_toggle);
        logger::debug("Teleport:            {}"sv, Globals::teleport);
        logger::debug("DisableOnStairs      {}"sv, Globals::disable_on_stairs);
        logger::debug("EnableNPCs:          {}"sv, Globals::enable_for_npcs);
        logger::debug("EnableAttackBlocking:{}"sv, Globals::enable_for_attacks);
        logger::debug("EnableDodgeBlocking: {}"sv, Globals::enable_for_dodges);
        logger::debug("EnableSlideBlocking: {}"sv, Globals::enable_for_slides);
        logger::debug("DropThreshold:       {:.2f}"sv, Globals::drop_threshold);
        logger::debug("LedgeDistance:       {:.2f}"sv, Globals::ledge_distance);
        logger::debug("JumpDuration         {:.2f}"sv, Globals::jump_duration);
        logger::debug("GroundLeeway         {:.2f}"sv, Globals::ground_leeway);
        logger::debug("MemoryDuration:      {}"sv, Globals::memory_duration);

        logger::debug("LoggingLevel:        {}"sv, Globals::log_level);

        ini.SetBoolValue("General", "UseTogglePower", Globals::use_spell_toggle,
                         "#If enabled, gives the player a power to toggle on/off ledge blocking.");

        const char *teleportComment = ("#Teleports the actor back to the last place they were not on a ledge."
                                       "\n#Prevents very fast animations from breaking free from ledges.");
        ini.SetBoolValue("General", "Teleport", Globals::teleport, teleportComment);

        const char *stairsComment = ("#Disable the ledge block while on stairs, this prevents rolling/attacking down stairs from being interfered with. Default true."
                                     "\n#Some stairs are not line of sight blocking (and therefore don't get hit by this mod's ray casts) and don't play well with this mod.");
        ini.SetBoolValue("General", "DisableOnStairs", Globals::disable_on_stairs, stairsComment);

        ini.SetBoolValue("General", "EnableNPCs", Globals::enable_for_npcs, "#Enable ledge blocking for NPCs, default true.");
        ini.SetBoolValue("General", "EnableAttackBlocking", Globals::enable_for_attacks, "#Enable ledge blocking for MCO/BFCO attacks");
        ini.SetBoolValue("General", "EnableDodgeBlocking", Globals::enable_for_dodges, "#Enable ledge blocking for DMCO/TUDM/TK dodges");
        ini.SetBoolValue("General", "EnableSlideBlocking", Globals::enable_for_slides, "#Enable ledge blocking for Crouch Slide");

        const char *dropThresholdComment = ("#How far the raycast needs to go before it is considered a drop 150.0 = 1.5x default player height"
                                            "\n#Max of 600.0, ray - casts of 600.0 are automatically considered as a ledge.");
        ini.SetDoubleValue("Tweaks", "DropThreshold", static_cast<double>(Globals::drop_threshold), dropThresholdComment);

        const char *ledgeDistanceComment = ("#How far should a ledge be detected. Default 25.0");
        ini.SetDoubleValue("Tweaks", "LedgeDistance", static_cast<double>(Globals::ledge_distance), ledgeDistanceComment);

        ini.SetDoubleValue("Tweaks", "JumpDuration", Globals::jump_duration,
                           "#How long after jumping should ledge blocker be disabled. Default is 1.5 seconds.");
        ini.SetDoubleValue("Tweaks", "GroundLeeway", static_cast<double>(Globals::ground_leeway),
                           "#How far the player should be off the ground before ledge detection shuts off. Default 90.0");

        const char *memoryDurationComment = ("#This stops the ledge detection from cutting off too early and dropping the actor off a ledge."
                                             "\n#Each check is ~11 milliseconds apart, default remembers for 10 checks.");
        ini.SetLongValue("Tweaks", "MemoryDuration", Globals::memory_duration, memoryDurationComment);

        ini.SetLongValue("Debug", "LoggingLevel", Globals::log_level,
                         "#0: Errors, 1: Warnings, 2: Info (default), 3: Debug, 4: Trace, 10: Trace + Markers");

        ini.SaveFile("Data\\SKSE\\Plugins\\AnimationLedgeBlockNG.ini");
    }
}