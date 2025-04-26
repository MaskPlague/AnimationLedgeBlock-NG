namespace logger = SKSE::log;

static bool debugMode = false;
static bool physicalBlocker = true;
static float dropThreshold = 150.0f; // 1.5x 1.0 player height
static float ledgeDistance = 50.0f;  // 50.0 units around the player
static float groundLeeway = 60.0f;
static int physicalBlockerType = 0;
static int memoryDuration = 10;

static bool isAttacking = false;
static bool isOnLedge = false;
static int loops = 0;
static bool isLooping = false;
static bool movedBlocker = false;
static RE::TESObjectREFR *ledgeBlocker;
// static RE::NiPoint3 previousPosition;

static float bestYaw = 0.0f;
static float bestDist = -1.0f;

static int untilMoveAgain = 0;
static int untilMomentHide = 0;

const int numRays = 12; // Number of rays to create.
constexpr int kRayMarkerCount = numRays;
static std::vector<RE::TESObjectREFR *> rayMarkers;

void SetupLog()
{
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder)
        SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);
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
    physicalBlocker = ini.GetBoolValue("General", "PhysicalBlocker", true);
    physicalBlockerType = ini.GetLongValue("General", "PhysicalBlockerType", 0);
    dropThreshold = static_cast<float>(ini.GetDoubleValue("General", "DropThreshold", 150.0f));
    if (dropThreshold > 600.0f)
        dropThreshold = 590.0f;
    ledgeDistance = static_cast<float>(ini.GetDoubleValue("General", "LedgeDistance", 50.0f));
    if (physicalBlocker && ledgeDistance < 50.0f)
        ledgeDistance = 50.0f;
    else if (!physicalBlocker && ledgeDistance < 10.0f)
        ledgeDistance = 10.0f;
    groundLeeway = static_cast<float>(ini.GetDoubleValue("General", "GroundLeeway", 60.0f));

    memoryDuration = ini.GetLongValue("General", "MemoryDuration", 10);
    if (memoryDuration < 1)
        memoryDuration = 1;

    debugMode = ini.GetBoolValue("Debug", "Enable", false);

    // Optionally write defaults back for any missing keys:
    ini.SetBoolValue("General", "PhysicalBlocker", physicalBlocker);
    ini.SetLongValue("General", "PhysicalBlockerType", physicalBlockerType);
    ini.SetDoubleValue("General", "DropThreshold", static_cast<double>(dropThreshold));
    ini.SetDoubleValue("General", "LedgeDistance", static_cast<double>(ledgeDistance));
    ini.SetDoubleValue("General", "GroundLeeway", static_cast<double>(groundLeeway));
    ini.SetLongValue("General", "MemoryDuration", memoryDuration);
    ini.SetBoolValue("Debug", "Enable", debugMode);
    ini.SaveFile("Data\\SKSE\\Plugins\\AnimationLedgeBlockNG.ini");
}

bool CreateLedgeBlocker()
{
    auto *handler = RE::TESDataHandler::GetSingleton();
    if (!handler)
    {
        logger::info("Error, could not get TESDataHandler");
        return true;
    }
    RE::TESObjectSTAT *blocker;
    switch (physicalBlockerType)
    {
    case 0: // Half Ring wall
        blocker = handler->LookupForm<RE::TESObjectSTAT>(0x800, "Animation Ledge Block NG.esp");
    case 1: // Full Ring
        blocker = handler->LookupForm<RE::TESObjectSTAT>(0x801, "Animation Ledge Block NG.esp");
    case 2: // Shallow wall
        blocker = handler->LookupForm<RE::TESObjectSTAT>(0x802, "Animation Ledge Block NG.esp");
    default: // Default Half Ring wall
        blocker = handler->LookupForm<RE::TESObjectSTAT>(0x800, "Animation Ledge Block NG.esp");
    }
    logger::trace("using blocker type {}", physicalBlockerType);
    if (!blocker)
    {
        logger::info("Error, Could not access Animation Ledge Block NG.esp");
        return true;
    }
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (!player)
    {
        logger::info("Error, Could not access the player");
        return true;
    }
    auto placed = player->PlaceObjectAtMe(blocker, true);
    placed->SetPosition(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() - 10000);
    ledgeBlocker = placed.get();
    return false;
}

// Call this once to spawn them near the player
void InitializeRayMarkers()
{
    auto *player = RE::PlayerCharacter::GetSingleton();

    if (!rayMarkers.empty())
    {
        return; // Already initialized
    }

    auto markerBase = RE::TESForm::LookupByID<RE::TESBoundObject>(0x00063B45);
    // meridias beacon 0004e4e6
    // garnet 00063B45
    if (!markerBase || !player)
    {
        return;
    }

    auto cell = player->GetParentCell();
    if (!cell)
    {
        return;
    }

    // Spawn markers near player and store references
    RE::NiPoint3 origin = player->GetPosition();

    for (int i = 0; i < kRayMarkerCount; ++i)
    {
        auto placed = player->PlaceObjectAtMe(markerBase, true);
        if (placed)
        {
            placed->SetPosition(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() + 50);
            rayMarkers.push_back(placed.get());
        }
    }
}

float GetPlayerDistanceToGround(auto player, auto world)
{
    RE::NiPoint3 playerPos = player->GetPosition();
    RE::NiPoint3 rayFrom = {playerPos.x, playerPos.y, playerPos.z + 80.0f};
    RE::NiPoint3 rayTo = rayFrom - RE::NiPoint3{0.0f, 0.0f, groundLeeway + 80.0f};
    const auto havokWorldScale = RE::bhkWorld::GetWorldScale();
    RE::bhkPickData pickData{};
    pickData.rayInput.from = rayFrom * havokWorldScale;
    pickData.rayInput.to = rayTo * havokWorldScale;
    uint32_t collisionFilterInfo = 0;
    player->GetCollisionFilterInfo(collisionFilterInfo);
    pickData.rayInput.filterInfo = (collisionFilterInfo & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kLOS);
    if (world->PickObject(pickData) && pickData.rayOutput.HasHit())
    {
        RE::NiPoint3 delta = rayTo - rayFrom;
        RE::NiPoint3 hitPos = rayFrom + delta * pickData.rayOutput.hitFraction;
        return playerPos.z - hitPos.z;
    }

    return 0.0f; // No hit = airborne or over void
}

float AverageAngles(const std::vector<float> &angles)
{
    float x = 0.0f;
    float y = 0.0f;

    for (float angle : angles)
    {
        x += std::cos(angle);
        y += std::sin(angle);
    }

    // Handle empty vector
    if (x == 0.0f && y == 0.0f)
        return 0.0f;

    return std::atan2(y, x);
}

float NormalizeAngle(float angle)
{
    return std::fmod((angle + 2 * RE::NI_PI), (2 * RE::NI_PI));
}

bool IsLedgeAhead()
{
    const auto player = RE::PlayerCharacter::GetSingleton();
    if (!player || player->AsActorState()->IsSwimming() || player->AsActorState()->IsFlying())
        return false;

    const auto cell = player->GetParentCell();
    if (!cell)
        return false;

    const auto bhkWorld = cell->GetbhkWorld();
    if (!bhkWorld)
        return false;

    const auto havokWorldScale = RE::bhkWorld::GetWorldScale();
    const float rayLength = 600.0f; // 600.0f

    const float maxStepUpHeight = 50.0f;   // not sure if this is working but I'm afraid to touch it
    const float directionThreshold = 0.7f; // Adjust for tighter/looser direction matching
    bestDist = -1.0f;

    RE::NiPoint3 playerPos = player->GetPosition();
    RE::NiPoint3 currentLinearVelocity;
    player->GetLinearVelocity(currentLinearVelocity);
    currentLinearVelocity.z = 0.0f; // z is not important
    float velocityLength = currentLinearVelocity.Length();
    RE::NiPoint3 moveDirection = {0.0f, 0.0f, 0.0f};
    if (velocityLength > 0.0f)
    {
        moveDirection = currentLinearVelocity / velocityLength;
    }

    // Yaw offsets to use for rays around the player
    float playerYaw = player->GetAngleZ();
    std::vector<float> yawOffsets;

    const float angleStep = static_cast<float>(2.0 * RE::NI_PI / static_cast<long double>(numRays));
    for (int i = 0; i < numRays; ++i)
    {
        yawOffsets.push_back(playerYaw + i * angleStep);
    }
    int i = 0; // increment into ray markers
    bool ledgeDetected = false;
    std::vector<float> validYaws;
    bool consider;
    for (float yaw : yawOffsets)
    {
        consider = true;
        RE::NiPoint3 dirVec(std::sin(yaw), std::cos(yaw), 0.0f);
        float dirLength = dirVec.Length();
        if (dirLength == 0.0f)
            continue;
        RE::NiPoint3 normalizedDir = dirVec / dirLength;

        // Skip if we're moving and this direction doesn't match our movement
        if (velocityLength > 0.0f)
        {
            float alignment = normalizedDir.Dot(moveDirection);
            if (alignment < directionThreshold)
            {
                if (!physicalBlocker)
                    continue;
                consider = false;
            }
        }
        else
            consider = false;

        RE::NiPoint3 rayFrom = playerPos + (normalizedDir * ledgeDistance) + RE::NiPoint3(0, 0, 80);
        RE::NiPoint3 rayTo = rayFrom + RE::NiPoint3(0, 0, -rayLength);

        RE::bhkPickData ray;
        ray.rayInput.from = rayFrom * havokWorldScale;
        ray.rayInput.to = rayTo * havokWorldScale;

        // Don't collide with player, from SkyParkourV2
        uint32_t collisionFilterInfo = 0;
        player->GetCollisionFilterInfo(collisionFilterInfo);
        ray.rayInput.filterInfo = (collisionFilterInfo & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kLOS);
        //---------------------------------------------

        if (bhkWorld->PickObject(ray) && ray.rayOutput.HasHit())
        {
            RE::NiPoint3 delta = rayTo - rayFrom;
            RE::NiPoint3 hitPos = rayFrom + delta * ray.rayOutput.hitFraction;

            if (debugMode) // if in debug mode move objects to ray hit positions
            {
                auto marker = rayMarkers[i];
                marker->SetPosition(hitPos.x, hitPos.y, hitPos.z + 20);
                i++;
            }
            if (hitPos.z > playerPos.z - maxStepUpHeight)
            {
                // logger::trace("Hit surface is above playerPos.z — step height");
                //  if (!physicalBlocker)
                //     return false;
                continue;
            }

            float verticalDrop = playerPos.z - hitPos.z;
            if (verticalDrop > dropThreshold && consider)
            {
                ledgeDetected = true;
                validYaws.push_back(yaw);
            }
        }
        else if (consider && physicalBlocker)
        {
            ledgeDetected = true;
            validYaws.push_back(yaw);
        }
    }
    if (!validYaws.empty())
    {
        float yaw = AverageAngles(validYaws);
        bestYaw = NormalizeAngle(yaw);
    }
    loops++;
    if (ledgeDetected || loops > memoryDuration)
    {
        isOnLedge = ledgeDetected;
        loops = 0;
    }
    if (player->IsInMidair())
    {
        auto distFromGround = GetPlayerDistanceToGround(player, bhkWorld);
        if (distFromGround == 0.0f)
        {
            ledgeDetected = false;
            loops = memoryDuration;
        }
    }
    return ledgeDetected;
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

// Force the player to stop moving toward their original vector
void StopPlayerVelocity()
{
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (!player)
        return;
    auto *controller = player->GetCharController();
    if (!controller)
        return;

    if (!physicalBlocker)
    {
        controller->SetLinearVelocityImpl({0.0f, 0.0f, 0.0f, 0.0f});
        auto pos = player->GetPosition();
        RE::NiPoint3 dirVec(std::sin(bestYaw), std::cos(bestYaw), 0.0f);
        auto backPos = pos - (dirVec * 4.0f);
        player->SetPosition(backPos, true);
    }
    if (physicalBlocker)
    {
        controller->SetLinearVelocityImpl({0.0f, 0.0f, 0.0f, 0.0f});
        if (!movedBlocker)
        {
            auto playerPos = player->GetPosition();
            RE::NiPoint3 objDir(std::sin(bestYaw), std::cos(bestYaw), 0.0f);
            objDir.Unitize();
            RE::NiPoint3 objPos = playerPos - (objDir * 10.0f);
            ledgeBlocker->SetPosition(objPos.x, objPos.y, playerPos.z + 60);
            movedBlocker = true;
            SetAngle(ledgeBlocker, {0.0f, 0.0f, bestYaw});
            ledgeBlocker->Update3DPosition(true);
        }
    }
}
void CellChangeCheck()
{
    if (!physicalBlocker)
        return;
    static RE::TESObjectCELL *lastPlayerCell;
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (!player)
        return;

    if (!ledgeBlocker)
        return;

    if (!ledgeBlocker->GetParentCell())
        return;
    auto *currentCell = player->GetParentCell();

    if (currentCell != lastPlayerCell)
    {
        lastPlayerCell = currentCell;
        if (ledgeBlocker)
        {
            ledgeBlocker->SetParentCell(currentCell);
            ledgeBlocker->SetPosition(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() - 10000.0f);
        }
    }
}
void EdgeCheck()
{
    if (!physicalBlocker)
    {
        if (IsLedgeAhead() && isAttacking || isOnLedge)
        {
            StopPlayerVelocity();
        }
    }
    else
    {
        if (IsLedgeAhead() && isAttacking)
        {
            untilMoveAgain++;
            StopPlayerVelocity();
        }
        else if (isAttacking)
            untilMoveAgain++;

        if (untilMoveAgain > 50)
        {
            untilMoveAgain = 0;
            movedBlocker = false;
            untilMomentHide++;
        }
        if (untilMomentHide > 3)
        {
            untilMomentHide = 0;
            movedBlocker = false;
            ledgeBlocker->SetPosition(ledgeBlocker->GetPositionX(), ledgeBlocker->GetPositionY(), -10000.0f);
        }
    }
}

bool IsGameWindowFocused()
{
    static const HWND gameWindow = ::FindWindow(nullptr, L"Skyrim Special Edition");
    return ::GetForegroundWindow() == gameWindow;
}
void LoopEdgeCheck()
{
    logger::debug("Loop starting");
    std::thread([&]
                {while (isAttacking || isOnLedge) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(11));
                    if (!IsGameWindowFocused())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Pause polling
                        continue;
                    }
                    SKSE::GetTaskInterface()->AddTask([&]{ EdgeCheck(); CellChangeCheck(); });
            } })
        .detach();
}

class AttackAnimationGraphEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent *event, RE::BSTEventSource<RE::BSAnimationGraphEvent> *)
    {
        if (!event)
        {
            return RE::BSEventNotifyControl::kStop;
        }
        // logger::trace("Payload: {}", event->payload);
        // logger::trace("Tag: {}\n", event->tag);
        static int type = 0;
        if (event->tag == "PowerAttack_Start_end" || event->tag == "MCO_DodgeInitiate" ||
            event->tag == "RollTrigger" || event->tag == "TKDR_DodgeStart")
        {
            isAttacking = true;
            logger::debug("\nAnimation Started");
            if (!isLooping)
                LoopEdgeCheck();
            isLooping = true;
            untilMoveAgain = 0;
            untilMomentHide = 0;
            if (event->tag == "PowerAttack_Start_end")
                type = 1;
            else if (event->tag == "MCO_DodgeInitiate")
                type = 2;
            else if (event->tag == "RollTrigger")
                type = 3;
            else if (event->tag == "TKDR_DodgeStart")
                type = 4;
        }
        else if (isAttacking && ((type == 1 && event->tag == "attackStop") || (type == 2 && event->payload == "$DMCO_Reset") ||
                                 (type == 3 && event->tag == "RollStop") || (type == 4 && event->tag == "TKDR_DodgeEnd")))
        {
            type = 0;
            isAttacking = false;
            isLooping = false;
            movedBlocker = false;
            if (physicalBlocker)
                ledgeBlocker->SetPosition(ledgeBlocker->GetPositionX(), ledgeBlocker->GetPositionY(), -10000.0f);
            logger::debug("\nAnimation Finished");
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    static AttackAnimationGraphEventSink *GetSingleton()
    {
        static AttackAnimationGraphEventSink singleton;
        return &singleton;
    }
};
void OnPostLoadGame()
{
    logger::info("Creating Event Sink");
    try
    {
        if (debugMode)
            InitializeRayMarkers();
        if (physicalBlocker)
            CreateLedgeBlocker();
        RE::PlayerCharacter::GetSingleton()->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
        logger::info("Event Sink Created");
        LoopEdgeCheck();
    }
    catch (...)
    {
        logger::warn("Failed to Create Event Sink");
    }
}

void MessageHandler(SKSE::MessagingInterface::Message *msg)
{
    if (msg->type != SKSE::MessagingInterface::kPostLoadGame)
        return;
    if (!bool(msg->data))
        return;
    OnPostLoadGame();
}

extern "C" DLLEXPORT bool SKSEPlugin_Load(const SKSE::LoadInterface *skse)
{
    SKSE::Init(skse);

    SetupLog();
    logger::info("Animation Ledge Block NG Plugin Starting");
    LoadConfig();
    if (debugMode)
        spdlog::set_level(spdlog::level::trace);
    else
        spdlog::set_level(spdlog::level::info);

    auto *messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener("SKSE", MessageHandler);

    logger::info("Animation Ledge Block NG Plugin Loaded");

    return true;
}
