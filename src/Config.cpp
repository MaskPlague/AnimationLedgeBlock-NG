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

        Globals::physical_blocker = ini.GetBoolValue("General", "PhysicalBlocker", false);
        Globals::physical_blocker_type = ini.GetLongValue("General", "PhysicalBlockerType", 2);
        Globals::disable_on_stairs = ini.GetBoolValue("General", "DisableOnStairs", true);
        Globals::enable_for_npcs = ini.GetBoolValue("General", "EnableNPCs", true);
        Globals::enable_for_attacks = ini.GetBoolValue("General", "EnableAttackBlocking", true);
        Globals::enable_for_dodges = ini.GetBoolValue("General", "EnableDodgeBlocking", true);
        Globals::enable_for_slides = ini.GetBoolValue("General", "EnableSlideBlocking", true);
        Globals::drop_threshold = static_cast<float>(ini.GetDoubleValue("Tweaks", "DropThreshold", 150.0f));
        if (Globals::drop_threshold > 600.0f)
            Globals::drop_threshold = 590.0f;
        Globals::ledge_distance = static_cast<float>(ini.GetDoubleValue("Tweaks", "LedgeDistance", 25.0f));
        if (Globals::physical_blocker && Globals::ledge_distance < 50.0f)
            Globals::ledge_distance = 50.0f;
        else if (!Globals::physical_blocker && Globals::ledge_distance < 10.0f)
            Globals::ledge_distance = 10.0f;
        Globals::ground_leeway = static_cast<float>(ini.GetDoubleValue("Tweaks", "GroundLeeway", 90.0f));
        Globals::jump_duration = static_cast<float>(ini.GetDoubleValue("Tweaks", "JumpDuration", Globals::jump_duration));
        Globals::memory_duration = ini.GetLongValue("Tweaks", "MemoryDuration", 10);
        Globals::memory_duration = std::max(Globals::memory_duration, 1);

        Globals::log_level = ini.GetLongValue("Debug", "LoggingLevel", 2);

        logger::debug("Version              {}"sv, SKSE::PluginDeclaration::GetSingleton()->GetVersion());
        logger::debug("Physical Blocker:    {}"sv, Globals::physical_blocker);
        logger::debug("PhysicalBlockerType: {}"sv, Globals::physical_blocker_type);
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

        const char *physcialBlockerComment = ("#PhysicalBlocker: true means use the invisible collider mesh, false is teleport behavior"
                                              "\n#Collider doesn't cause jitter but allows the player to pass more easily"
                                              "\n#Teleport causes jitter but is smoother in most other ways ");
        ini.SetBoolValue("General", "PhysicalBlocker", Globals::physical_blocker, physcialBlockerComment);

        const char *physcialBlockerTypeComment = ("#0 = Half Ring Wall, half a tall ring"
                                                  "\n# 1 = Full Ring, a ring around the player"
                                                  "\n# 2 = Shallow Wall, basically a shallow ^ shape");
        ini.SetLongValue("General", "PhysicalBlockerType", Globals::physical_blocker_type, physcialBlockerTypeComment);

        const char *stairsComment = ("#Disable the ledge block while on stairs, this prevents rolling/attacking down stairs from being interfered with. Default true."
                                     "\n#Some stairs are not line of sight blocking(and therefore don 't get hit by this mod' s ray - casts) and don't play well with this mod.");
        ini.SetBoolValue("General", "DisableOnStairs", Globals::disable_on_stairs, stairsComment);

        ini.SetBoolValue("General", "EnableNPCs", Globals::enable_for_npcs, "#Enable ledge blocking for NPCs, default true.");
        ini.SetBoolValue("General", "EnableAttackBlocking", Globals::enable_for_attacks, "#Enable ledge blocking for MCO/BFCO attacks");
        ini.SetBoolValue("General", "EnableDodgeBlocking", Globals::enable_for_dodges, "#Enable ledge blocking for DMCO/TUDM/TK dodges");
        ini.SetBoolValue("General", "EnableSlideBlocking", Globals::enable_for_slides, "#Enable ledge blocking for Crouch Slide");

        const char *dropThresholdComment = ("#How far the raycast needs to go before it is considered a drop 150.0 = 1.5x default player height"
                                            "\n#Max of 600.0, ray - casts of 600.0 are automatically considered as a ledge.");
        ini.SetDoubleValue("Tweaks", "DropThreshold", static_cast<double>(Globals::drop_threshold), dropThresholdComment);

        const char *ledgeDistanceComment = ("#How far should a ledge be detected. Default 25.0"
                                            "\n# 10.0 min for teleporting(less looks better, but will put you closer to the ledge with potential to slip past)"
                                            "\n# 50.0 min for physical(too little and it won't detect fast enough)");
        ini.SetDoubleValue("Tweaks", "LedgeDistance", static_cast<double>(Globals::ledge_distance), ledgeDistanceComment);

        ini.SetDoubleValue("Tweaks", "JumpDuration", Globals::jump_duration,
                           "#How long after jumping should ledge blocker be disabled. Default is 1.5 seconds.");
        ini.SetDoubleValue("Tweaks", "GroundLeeway", static_cast<double>(Globals::ground_leeway),
                           "#How far the player should be off the ground before ledge detection shuts off. Default 90.0");

        const char *memoryDurationComment = ("#This stops the ledge detection from cutting off too early and dropping the player off a ledge."
                                             "\n#Each check is 11 milliseconds apart, default remembers for 10 checks.");
        ini.SetLongValue("Tweaks", "MemoryDuration", Globals::memory_duration, memoryDurationComment);

        ini.SetLongValue("Debug", "LoggingLevel", Globals::log_level,
                         "#0: Errors, 1: Warnings, 2: Info (default), 3: Debug, 4: Trace, 10: Trace + Markers");

        ini.SaveFile("Data\\SKSE\\Plugins\\AnimationLedgeBlockNG.ini");
    }
}