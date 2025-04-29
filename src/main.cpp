namespace logger = SKSE::log;

static bool showMarkers = false;
static int logLevel = 2;
static bool physicalBlocker = true;
static bool enableForNPCs = true;
static float dropThreshold = 150.0f; // 1.5x 1.0 player height
static float ledgeDistance = 50.0f;  // 50.0 units around the player
static float groundLeeway = 60.0f;
static int physicalBlockerType = 0;
static int memoryDuration = 10;

const int numRays = 12; // Number of rays to create.
constexpr int kRayMarkerCount = (numRays / 2) + numRays;

struct ActorState
{
    bool isAttacking = false;
    bool isOnLedge = false;
    int loops = 0;
    bool isLooping = false;
    bool movedBlocker = false;

    float bestYaw = 0.0f;
    float bestDist = -1.0f;

    int untilMoveAgain = 0;
    int untilMomentHide = 0;

    int animationType = 0;

    RE::TESObjectCELL *lastActorCell;

    RE::TESObjectREFR *ledgeBlocker;

    std::vector<RE::TESObjectREFR *> rayMarkers;
};

inline std::unordered_map<RE::FormID, ActorState> g_actorStates;

ActorState &GetState(RE::Actor *actor)
{
    return g_actorStates[actor->GetFormID()];
}

void CleanupActors()
{
    for (auto it = g_actorStates.begin(); it != g_actorStates.end();)
    {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
        if (!actor || actor->IsDead() || actor->IsDeleted() || !actor->IsInCombat() || actor->IsDisabled())
        {
            if (actor->IsPlayerRef())
            {
                ++it;
                continue;
            }
            it = g_actorStates.erase(it);
        }
        else
            ++it;
    }
}

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

void SetLogLevel()
{
    switch (logLevel)
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
        showMarkers = true;
        break;
    default:
        logLevel = 2;
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

    enableForNPCs = ini.GetBoolValue("General", "EnableNPCs", true);

    memoryDuration = ini.GetLongValue("General", "MemoryDuration", 10);
    if (memoryDuration < 1)
        memoryDuration = 1;

    logLevel = ini.GetLongValue("Debug", "LoggingLevel", 2);
    logger::debug("Version              {}", SKSE::PluginDeclaration::GetSingleton()->GetVersion());
    logger::debug("PhyscialBlocker:     {}", physicalBlocker);
    logger::debug("PhyscialBlockerType: {}", physicalBlockerType);
    logger::debug("DropThreshold:       {:.2f}", dropThreshold);
    logger::debug("LedgeDistance:       {:.2f}", ledgeDistance);
    logger::debug("GroundLeeway         {:.2f}", groundLeeway);
    logger::debug("MemoryDuration:      {}", memoryDuration);
    logger::debug("EnableNPCs:          {}", enableForNPCs);
    logger::debug("LoggingLevel:        {}", logLevel);

    // Optionally write defaults back for any missing keys:
    ini.SetBoolValue("General", "PhysicalBlocker", physicalBlocker);
    ini.SetLongValue("General", "PhysicalBlockerType", physicalBlockerType);
    ini.SetDoubleValue("General", "DropThreshold", static_cast<double>(dropThreshold));
    ini.SetDoubleValue("General", "LedgeDistance", static_cast<double>(ledgeDistance));
    ini.SetDoubleValue("General", "GroundLeeway", static_cast<double>(groundLeeway));
    ini.SetLongValue("General", "MemoryDuration", memoryDuration);
    ini.SetBoolValue("General", "EnableNPCs", enableForNPCs);
    ini.SetLongValue("Debug", "LoggingLevel", logLevel);
    ini.SaveFile("Data\\SKSE\\Plugins\\AnimationLedgeBlockNG.ini");
}

bool CreateLedgeBlocker(RE::Actor *actor)
{
    auto *handler = RE::TESDataHandler::GetSingleton();
    if (!handler || !actor)
    {
        logger::info("Error, could not get TESDataHandler");
        return true;
    }
    RE::TESObjectSTAT *blocker;
    switch (physicalBlockerType)
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
    logger::trace("using blocker type {}", physicalBlockerType);
    if (!blocker)
    {
        logger::warn("Could not access Animation Ledge Block NG.esp");
        return true;
    }
    auto placed = actor->PlaceObjectAtMe(blocker, true);
    placed->SetPosition(actor->GetPositionX(), actor->GetPositionY(), actor->GetPositionZ() - 10000);
    auto &state = GetState(actor);
    state.ledgeBlocker = placed.get();
    return false;
}

// Call this once to spawn them near the actor
void InitializeRayMarkers(RE::Actor *actor)
{
    auto &state = GetState(actor);
    if (!state.rayMarkers.empty())
    {
        return; // Already initialized
    }

    auto markerBase = RE::TESForm::LookupByID<RE::TESBoundObject>(0x00063B45);
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

    for (int i = 0; i < kRayMarkerCount; ++i)
    {
        auto placed = actor->PlaceObjectAtMe(markerBase, true);
        if (placed)
        {
            placed->SetPosition(actor->GetPositionX(), actor->GetPositionY(), actor->GetPositionZ() + 50);
            state.rayMarkers.push_back(placed.get());
        }
    }
}

float GetActorDistanceToGround(auto actor, auto world)
{
    RE::NiPoint3 actorPos = actor->GetPosition();
    RE::NiPoint3 rayFrom = {actorPos.x, actorPos.y, actorPos.z + 80.0f};
    RE::NiPoint3 rayTo = rayFrom - RE::NiPoint3{0.0f, 0.0f, groundLeeway + 80.0f};
    const auto havokWorldScale = RE::bhkWorld::GetWorldScale();
    RE::bhkPickData pickData{};
    pickData.rayInput.from = rayFrom * havokWorldScale;
    pickData.rayInput.to = rayTo * havokWorldScale;
    uint32_t collisionFilterInfo = 0;
    actor->GetCollisionFilterInfo(collisionFilterInfo);
    pickData.rayInput.filterInfo = (collisionFilterInfo & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kLOS);
    if (world->PickObject(pickData) && pickData.rayOutput.HasHit())
    {
        RE::NiPoint3 delta = rayTo - rayFrom;
        RE::NiPoint3 hitPos = rayFrom + delta * pickData.rayOutput.hitFraction;
        return actorPos.z - hitPos.z;
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

float MaxZDist(std::vector<RE::NiPoint3> &hits)
{
    if (hits.empty())
        return 0.0f;

    float max = hits[0].z;
    float min = hits[0].z;
    for (RE::NiPoint3 hit : hits)
    {
        if (hit.z > max)
            max = hit.z;
        else if (hit.z < min)
            min = hit.z;
    }
    return max - min;
}

bool IsLedgeAhead(RE::Actor *actor, ActorState &state)
{
    if (!actor || actor->AsActorState()->IsSwimming() || actor->AsActorState()->IsFlying())
    {
        logger::warn("Either could not get actor or actor is swimming or on dragon.");
        return false;
    }

    const auto cell = actor->GetParentCell();
    if (!cell)
    {
        logger::warn("Ledge check couldn't get parent cell");
        return false;
    }

    const auto bhkWorld = cell->GetbhkWorld();
    if (!bhkWorld)
    {
        logger::warn("Ledge check couldn't get bhkWorld");
        return false;
    }

    const auto havokWorldScale = RE::bhkWorld::GetWorldScale();
    const float rayLength = 600.0f; // 600.0f

    // const float maxStepUpHeight = 50.0f;   // not sure if this is working but I'm afraid to touch it
    const float directionThreshold = 0.7f; // Adjust for tighter/looser direction matching
    state.bestDist = -1.0f;
    RE::NiPoint3 actorPos = actor->GetPosition();
    RE::NiPoint3 currentLinearVelocity;
    actor->GetLinearVelocity(currentLinearVelocity);
    currentLinearVelocity.z = 0.0f; // z is not important
    float velocityLength = currentLinearVelocity.Length();

    // Filter out actor collision from rays
    uint32_t collisionFilterInfo = 0;
    actor->GetCollisionFilterInfo(collisionFilterInfo);
    uint32_t filterInfo = (collisionFilterInfo & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kLOS);

    RE::NiPoint3 moveDirection = {0.0f, 0.0f, 0.0f};
    if (velocityLength > 0.0f)
    {
        moveDirection = currentLinearVelocity / velocityLength;
    }
    // Yaw offsets to use for rays around the actor
    float actorYaw = actor->GetAngleZ();
    std::vector<float> yawOffsets;
    float angleStep = static_cast<float>(2.0 * RE::NI_PI / static_cast<long double>(numRays));
    for (int i = 0; i < numRays; ++i)
    {
        yawOffsets.push_back(actorYaw + i * angleStep);
    }
    if (actor->IsInMidair())
    {
        auto distFromGround = GetActorDistanceToGround(actor, bhkWorld);
        // If over a certain distance from ground, create and cast far rays to get slope of terrain
        if (distFromGround == 0.0f)
        {
            std::vector<RE::NiPoint3> hitPositions;
            for (float yaw : yawOffsets)
            {
                RE::NiPoint3 dirVec(std::sin(yaw), std::cos(yaw), 0.0f);
                float dirLength = dirVec.Length();
                if (dirLength == 0.0f)
                    continue;
                RE::NiPoint3 normalizedDir = dirVec / dirLength;
                RE::NiPoint3 rayFrom = actorPos + (normalizedDir * 200.0f) + RE::NiPoint3(0, 0, 80);
                RE::NiPoint3 rayTo = rayFrom + RE::NiPoint3(0, 0, -rayLength);

                RE::bhkPickData ray;
                ray.rayInput.from = rayFrom * havokWorldScale;
                ray.rayInput.to = rayTo * havokWorldScale;
                ray.rayInput.filterInfo = filterInfo;

                if (bhkWorld->PickObject(ray) && ray.rayOutput.HasHit())
                {
                    RE::NiPoint3 delta = rayTo - rayFrom;
                    RE::NiPoint3 hitPos = rayFrom + delta * ray.rayOutput.hitFraction;
                    if (hitPos.z > actorPos.z + 40.0F)
                        continue;
                    hitPositions.push_back(hitPos);
                }
                // else
                //     hitPositions.push_back(RE::NiPoint3{0.0f, 0.0f, actorPos.z - 600.0f});
            }
            auto heightDiff = MaxZDist(hitPositions);
            if ((0.01f < heightDiff) && (heightDiff < 300.0f))
            {
                logger::trace("HeightDiff of {} is less than 300.0f, returing false", heightDiff);
                return false;
            }
        }
    }
    int i = 0; // increment into ray markers
    bool ledgeDetected = false;
    std::vector<float> validYaws;
    // int continued = 0;
    for (float yaw : yawOffsets)
    {
        RE::NiPoint3 dirVec(std::sin(yaw), std::cos(yaw), 0.0f);
        float dirLength = dirVec.Length();
        if (dirLength == 0.0f)
        {
            // continued++;
            // logger::trace(" Skipping ray as dirLength is 0.0f, {}.", continued);
            continue;
        }
        RE::NiPoint3 normalizedDir = dirVec / dirLength;

        // Skip if we're moving and this direction doesn't match our movement
        if (velocityLength > 0.0f)
        {
            float alignment = normalizedDir.Dot(moveDirection);
            if (alignment < directionThreshold)
            {
                // continued++;
                // logger::trace(" Skipping ray as it is not in correct direction, {}.", continued);
                continue;
            }
        }
        else
        {
            // continued++;
            // logger::trace(" Skipping ray as the velocity is not above 0.0f, {}", continued);
            continue;
        }

        RE::NiPoint3 rayFrom = actorPos + (normalizedDir * ledgeDistance) + RE::NiPoint3(0, 0, 80);
        RE::NiPoint3 rayTo = rayFrom + RE::NiPoint3(0, 0, -rayLength);

        RE::bhkPickData ray;
        ray.rayInput.from = rayFrom * havokWorldScale;
        ray.rayInput.to = rayTo * havokWorldScale;
        ray.rayInput.filterInfo = filterInfo;

        if (bhkWorld->PickObject(ray) && ray.rayOutput.HasHit())
        {
            RE::NiPoint3 delta = rayTo - rayFrom;
            RE::NiPoint3 hitPos = rayFrom + delta * ray.rayOutput.hitFraction;

            if (showMarkers) // if in debug mode move objects to ray hit positions
            {
                auto marker = state.rayMarkers[i];
                if (marker)
                    marker->SetPosition(hitPos.x, hitPos.y, hitPos.z + 20);
                ++i;
            }
            if (hitPos.z > actorPos.z - 50.0f)
            {
                // logger::trace("Hit surface is above actorPos.z - 50.0f");
                // continued++;
                continue;
            }

            float verticalDrop = actorPos.z - hitPos.z;
            if (verticalDrop > dropThreshold)
            {
                // logger::trace("Hit below drop threshold, cliff detected");
                ledgeDetected = true;
                validYaws.push_back(yaw);
            }
        }
        else if (physicalBlocker)
        {
            // logger::trace("Ray missed, must be cliff.");
            ledgeDetected = true;
            validYaws.push_back(yaw);
        }
    }
    if (!validYaws.empty())
    {
        float yaw = AverageAngles(validYaws);
        state.bestYaw = NormalizeAngle(yaw);
    }
    ++state.loops;
    if (ledgeDetected || state.loops > memoryDuration)
    {
        state.isOnLedge = ledgeDetected;
        state.loops = 0;
    }
    // if (continued == numRays)
    //     logger::trace(" All rays skipped");
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

// Force the actor to stop moving toward their original vector
void StopActorVelocity(RE::Actor *actor, ActorState &state)
{
    auto *controller = actor->GetCharController();
    if (!controller)
    {
        logger::error("Could not get actor character controller");
        return;
    }

    controller->SetLinearVelocityImpl({0.0f, 0.0f, 0.0f, 0.0f});
    if (!physicalBlocker)
    {
        logger::debug("Teleportation blocking {}", actor->GetName());
        auto pos = actor->GetPosition();
        RE::NiPoint3 dirVec(std::sin(state.bestYaw), std::cos(state.bestYaw), 0.0f);
        auto backPos = pos - (dirVec * 4.0f);
        actor->SetPosition(backPos, true);
    }
    else
    {
        if (!state.movedBlocker)
        {
            logger::debug("Moving a physcial blocker to {}", actor->GetName());
            auto actorPos = actor->GetPosition();
            RE::NiPoint3 objDir(std::sin(state.bestYaw), std::cos(state.bestYaw), 0.0f);
            objDir.Unitize();
            RE::NiPoint3 objPos = actorPos - (objDir * 10.0f);
            state.ledgeBlocker->SetPosition(objPos.x, objPos.y, actorPos.z + 60);
            state.movedBlocker = true;
            SetAngle(state.ledgeBlocker, {0.0f, 0.0f, state.bestYaw});
            state.ledgeBlocker->Update3DPosition(true);
        }
    }
}
void CellChangeCheck(RE::Actor *actor)
{
    auto &state = GetState(actor);
    if (!physicalBlocker)
        return;

    if (!actor)
        return;

    if (!state.ledgeBlocker)
        return;

    if (!state.ledgeBlocker->GetParentCell())
        return;
    auto *currentCell = actor->GetParentCell();

    if (currentCell != state.lastActorCell)
    {
        state.lastActorCell = currentCell;
        if (state.ledgeBlocker)
        {
            state.ledgeBlocker->SetParentCell(currentCell);
            state.ledgeBlocker->SetPosition(actor->GetPositionX(), actor->GetPositionY(), actor->GetPositionZ() - 10000.0f);
        }
    }
}

void EdgeCheck(RE::Actor *actor)
{
    auto &state = GetState(actor);
    if (!physicalBlocker)
    {
        // logger::trace("Checking for ledge.");
        if (IsLedgeAhead(actor, state) && state.isAttacking || state.isOnLedge)
        {
            // logger::trace("Stopping actor velocity.");
            StopActorVelocity(actor, state);
        }
    }
    else
    {
        if (IsLedgeAhead(actor, state) && state.isAttacking)
        {
            ++state.untilMoveAgain;
            StopActorVelocity(actor, state);
        }
        else if (state.isAttacking)
            ++state.untilMoveAgain;

        if (state.untilMoveAgain > 50)
        {
            state.untilMoveAgain = 0;
            state.movedBlocker = false;
            ++state.untilMomentHide;
        }
        if (state.untilMomentHide > 3)
        {
            state.untilMomentHide = 0;
            state.movedBlocker = false;
            state.ledgeBlocker->SetPosition(state.ledgeBlocker->GetPositionX(), state.ledgeBlocker->GetPositionY(), -10000.0f);
        }
    }
}

bool IsGameWindowFocused()
{
    static const HWND gameWindow = ::FindWindow(nullptr, L"Skyrim Special Edition");
    return ::GetForegroundWindow() == gameWindow;
}

void LoopEdgeCheck(RE::Actor *actor)
{
    if (!actor)
        return;
    logger::trace("Starting Edge check loop");
    std::thread([actor]()
                {
        RE::FormID formID = actor->GetFormID();

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(11));
            
            if (!IsGameWindowFocused())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                //logger::trace("Window is tabbed out, stalling loop");
                continue;
            }
            auto it = g_actorStates.find(formID);
            if (it == g_actorStates.end()) {
                //logger::debug("Actor state no longer exists, ending LoopEdgeCheck");
                break;
            }

            auto& state = it->second;
            if (!(state.isAttacking || state.isOnLedge)) {
                break;
            }

            SKSE::GetTaskInterface()->AddTask([actor]() {
                EdgeCheck(actor);
                CellChangeCheck(actor);
            });
        } })
        .detach();
}

class AttackAnimationGraphEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
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
        auto holderName = event->holder->GetName();
        auto &state = GetState(actor);
        logger::trace("{} Payload: {}", holderName, event->payload);
        logger::trace("{} Tag: {}", holderName, event->tag);
        if (event->tag == "PowerAttack_Start_end" || event->tag == "MCO_DodgeInitiate" ||
            event->tag == "RollTrigger" || event->tag == "TKDR_DodgeStart" ||
            event->tag == "MCO_DisableSecondDodge")
        {
            state.isAttacking = true;
            logger::debug("Animation Started for {}", holderName);
            if (!state.isLooping)
                LoopEdgeCheck(actor);
            state.isLooping = true;
            state.untilMoveAgain = 0;
            state.untilMomentHide = 0;
            if (event->tag == "PowerAttack_Start_end")
                state.animationType = 1;
            else if (event->tag == "MCO_DodgeInitiate")
                state.animationType = 2;
            else if (event->tag == "RollTrigger")
                state.animationType = 3;
            else if (event->tag == "TKDR_DodgeStart")
                state.animationType = 4;
            else if (event->tag == "MCO_DisableSecondDodge")
                state.animationType = 5;
        }
        else if (state.isAttacking && ((state.animationType == 1 && event->tag == "attackStop") || (state.animationType == 2 && event->payload == "$DMCO_Reset") ||
                                       (state.animationType == 3 && event->tag == "RollStop") || (state.animationType == 4 && event->tag == "TKDR_DodgeEnd") ||
                                       (state.animationType == 5 && event->tag == "EnableBumper")))
        {
            state.animationType = 0;
            state.isAttacking = false;
            state.isLooping = false;
            state.movedBlocker = false;
            if (physicalBlocker)
                state.ledgeBlocker->SetPosition(state.ledgeBlocker->GetPositionX(), state.ledgeBlocker->GetPositionY(), -10000.0f);
            logger::debug("Animation Finished for {}", holderName);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    static AttackAnimationGraphEventSink *GetSingleton()
    {
        static AttackAnimationGraphEventSink singleton;
        return &singleton;
    }
};

class CombatEventSink : public RE::BSTEventSink<RE::TESCombatEvent>
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
        if (combatState == RE::ACTOR_COMBAT_STATE::kCombat)
        {
            auto &state = g_actorStates[formID];
            if (!state.ledgeBlocker && physicalBlocker)
            {
                CreateLedgeBlocker(actor);
            }
            if (state.rayMarkers.empty() && showMarkers)
                InitializeRayMarkers(actor);
            actor->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
            logger::debug("Tracking new combat actor: {}", actor->GetName());
        }
        else if (combatState == RE::ACTOR_COMBAT_STATE::kNone)
        {
            auto it = g_actorStates.find(formID);
            if (it != g_actorStates.end())
            {
                if (it->second.ledgeBlocker)
                {
                    it->second.ledgeBlocker->Disable();
                    it->second.ledgeBlocker->SetPosition(
                        it->second.ledgeBlocker->GetPositionX(),
                        it->second.ledgeBlocker->GetPositionY(),
                        -10000.0f);
                }
                g_actorStates.erase(it);
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
        g_actorStates.clear();
        auto player = RE::PlayerCharacter::GetSingleton();
        player->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
        auto &state = g_actorStates[player->GetFormID()];
        if (!state.ledgeBlocker && physicalBlocker)
        {
            CreateLedgeBlocker(player);
        }
        if (state.rayMarkers.empty() && showMarkers)
            InitializeRayMarkers(player);
        if (enableForNPCs)
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
    logger::debug("Recieved PostLoadGame message");
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

    SetupLog();
    logger::info("Animation Ledge Block NG Plugin Starting");
    LoadConfig();
    SetLogLevel();

    SKSE::GetMessagingInterface()->RegisterListener("SKSE", MessageHandler);

    logger::info("Animation Ledge Block NG Plugin Loaded");

    return true;
}