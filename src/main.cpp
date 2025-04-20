#include <spdlog/sinks/basic_file_sink.h>

namespace logger = SKSE::log;

bool debugMode = false;

bool isAttacking = false;
bool eventSinkStarted = false;
RE::NiPoint3 globalPlayerPos = RE::NiPoint3();

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
constexpr int kRayMarkerCount = 9;
static std::vector<RE::TESObjectREFR *> rayMarkers;

// Call this once to spawn them near the player
void InitializeRayMarkers()
{
    auto *player = RE::PlayerCharacter::GetSingleton();

    if (!rayMarkers.empty())
    {
        return; // Already initialized
    }

    auto markerBase = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0004e4e6);
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
        player->PlaceObjectAtMe(markerBase, true);
        auto placed = player->PlaceObjectAtMe(markerBase, true);
        if (placed)
        {
            placed->SetPosition(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() + 200);
            rayMarkers.push_back(placed.get());
        }
    }
}
// Method base comes from SkyParkourV2
bool IsLedgeAhead()
{
    const auto player = RE::PlayerCharacter::GetSingleton();
    if (!player)
        return false;
    const auto cell = player->GetParentCell();
    if (!cell)
        return false;

    const auto bhkWorld = cell->GetbhkWorld();
    if (!bhkWorld)
        return false;

    const auto havokWorldScale = RE::bhkWorld::GetWorldScale();
    const float rayLength = 600.0f;
    const float ledgeDistance = 50.0f;
    const float dropThreshold = 200.0f;
    const float maxStepUpHeight = 50.0f;

    RE::NiPoint3 playerPos = player->GetPosition();
    RE::NiPoint3 currentLinearVelocity;
    player->GetLinearVelocity(currentLinearVelocity);
    currentLinearVelocity.z = 0.0f;
    float velocityLength = currentLinearVelocity.Length();
    if (velocityLength > 0.0f)
    {
        RE::NiPoint3 moveDirection = currentLinearVelocity / velocityLength;
        globalPlayerPos = playerPos - (moveDirection * 5.0f);
    }
    else
        globalPlayerPos = playerPos;
    // Calculate forward vector from player's yaw
    // float baseYaw = player->data.angle.z;
    std::vector<float> yawOffsets;
    const int numRays = 9; // 360° / 9
    const float angleStep = static_cast<float>(2.0 * std::_Pi_val / static_cast<long double>(numRays));

    for (int i = 0; i < numRays; ++i)
    {
        yawOffsets.push_back(i * angleStep);
    }
    int i = 0;
    for (float yaw : yawOffsets)
    {
        RE::NiPoint3 dirVec(std::sin(yaw), std::cos(yaw), 0.0f);

        RE::NiPoint3 rayFrom = playerPos + (dirVec * ledgeDistance) + RE::NiPoint3(0, 0, 250);
        RE::NiPoint3 rayTo = rayFrom + RE::NiPoint3(0, 0, -rayLength);

        RE::bhkPickData ray;
        ray.rayInput.from = rayFrom * havokWorldScale;
        ray.rayInput.to = rayTo * havokWorldScale;

        // Set up collision filter to exclude the player
        uint32_t collisionFilterInfo = 0;
        player->GetCollisionFilterInfo(collisionFilterInfo);
        ray.rayInput.filterInfo = (collisionFilterInfo & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kLOS);

        // Perform raycast
        if (bhkWorld->PickObject(ray) && ray.rayOutput.HasHit())
        {
            // float fraction = std::clamp(ray.rayOutput.hitFraction, 0.0f, 1.0f);
            RE::NiPoint3 delta = rayTo - rayFrom;
            RE::NiPoint3 hitPos = rayFrom + delta * ray.rayOutput.hitFraction;
            if (debugMode)
            {
                auto marker = rayMarkers[i];
                marker->SetPosition(hitPos.x, hitPos.y, hitPos.z + 20);
                i = i + 1;
            }
            /*const uint32_t layerIndex = ray.rayOutput.rootCollidable->broadPhaseHandle.collisionFilterInfo & 0x7F;
            RE::COL_LAYER layerHit = static_cast<RE::COL_LAYER>(layerIndex);
            switch (layerHit)
            {
            case RE::COL_LAYER::kStatic:
            case RE::COL_LAYER::kCollisionBox:
            case RE::COL_LAYER::kTerrain:
            case RE::COL_LAYER::kGround:
            case RE::COL_LAYER::kProps:
            case RE::COL_LAYER::kDoorDetection:
            case RE::COL_LAYER::kTrees:
            case RE::COL_LAYER::kClutterLarge:
            case RE::COL_LAYER::kAnimStatic:
            case RE::COL_LAYER::kDebrisLarge:
                break; // Valid ground layers
            default:
                logger::trace("Ignored non-ground layer.");
                return 0.0f;
            }*/
            logger::trace("player z {}", playerPos.z);
            bool debugTest = true;
            if (hitPos.z > playerPos.z - maxStepUpHeight)
            {
                logger::trace("Hit surface is above playerPos.z — likely a wall.");
                if (debugMode)
                    debugTest = false;
                else
                    return false;
            }

            float verticalDrop = playerPos.z - hitPos.z;
            logger::trace("Ledge drop at {:.2f} units ahead: {:.2f} units down", ledgeDistance, verticalDrop);

            if (verticalDrop > dropThreshold && debugTest)
            {
                logger::trace("Ledge detected!");
                return true;
            }
        }
        else
        {
            logger::trace("No surface hit ahead — definite ledge.");
            return true;
        }
    }
    return false;
}
void StopPlayerVelocity()
{
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (!player)
        return;
    player->StopMoving(0.0f);
    if (auto *controller = player->GetCharController(); controller)
    {
        controller->SetLinearVelocityImpl(RE::hkVector4());
    }
    player->SetPosition(globalPlayerPos, true);
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
        logger::debug("Payload: {}", event->payload);
        logger::debug("Tag: {}\n", event->tag);
        if (!isAttacking && (event->tag == "PowerAttack_Start_end" || event->tag == "MCO_DodgeInitiate" || event->tag == "RollTrigger"))
        {
            isAttacking = true;
            logger::debug("Animation Started");
        }
        else if (isAttacking && (event->tag == "attackStop" || event->payload == "$DMCO_Reset" || event->tag == "RollStop"))
        {
            isAttacking = false;
            logger::debug("Animation Finished");
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

void EdgeCheck()
{
    if (IsLedgeAhead() && isAttacking)
    {
        StopPlayerVelocity();
        logger::trace("Cliff detected");
    }
    else
    {
        globalPlayerPos = RE::NiPoint3();
    }
}

void LoopEdgeCheck()
{
    logger::debug("Loop starting");
    std::thread([&]
                {while (true) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(11));
                    SKSE::GetTaskInterface()->AddTask([&]{ EdgeCheck(); });
            } })
        .detach();
}

void OnPostLoadGame()
{
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (player)
    {
        logger::info("Creating Event Sink");
        try
        {
            if (debugMode)
                InitializeRayMarkers();
            auto *sink = new AttackAnimationGraphEventSink();
            player->AddAnimationGraphEventSink(sink);
            logger::info("Event Sink Created");
            LoopEdgeCheck();
        }
        catch (...)
        {
            logger::info("Failed to Create Event Sink");
        }
    }
    else
        logger::info("Failed to Create Event Sink as Player Could not be Retrieved");
}

void MessageHandler(SKSE::MessagingInterface::Message *msg)
{
    if (msg->type == SKSE::MessagingInterface::kPostLoadGame)
    {
        OnPostLoadGame();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse)
{
    SKSE::Init(skse);

    SetupLog();
    if (debugMode)
        spdlog::set_level(spdlog::level::debug);
    else
        spdlog::set_level(spdlog::level::info);

    logger::info("Animation Ledge Block NG Plugin Starting");

    auto *messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener("SKSE", MessageHandler);

    logger::info("Animation Ledge Block NG Plugin Loaded");

    return true;
}
