#include <spdlog/sinks/basic_file_sink.h>

namespace logger = SKSE::log;

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

// Code mostly comes from SkyParkourV2
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
    const float rayLength = 520.0f;
    const float ledgeDistance = 50.0f;
    const float dropThreshold = 200.0f;
    const float maxStepUpHeight = 50.0f;

    RE::NiPoint3 playerPos = player->GetPosition();

    // Calculate forward vector from player's yaw
    float yaw = player->data.angle.z;
    RE::NiPoint3 forwardVec(std::sin(yaw), std::cos(yaw), 0.0f);
    logger::trace("forward vector x{} y{} z{}", forwardVec.x, forwardVec.y, forwardVec.z);
    // Setup ray origin slightly in front of player
    RE::NiPoint3 rayUnscaledFrom = playerPos + (forwardVec * ledgeDistance) + RE::NiPoint3(0, 0, 100.0f);
    RE::NiPoint3 rayUnscaledTo = rayUnscaledFrom + RE::NiPoint3(0, 0, -rayLength);

    // How fast to move from the ledge
    globalPlayerPos = playerPos - (forwardVec * 5);

    RE::bhkPickData ray;
    ray.rayInput.from = rayUnscaledFrom * havokWorldScale;
    ray.rayInput.to = rayUnscaledTo * havokWorldScale;

    // Set up collision filter to exclude the player
    uint32_t collisionFilterInfo = 0;
    player->GetCollisionFilterInfo(collisionFilterInfo);
    ray.rayInput.filterInfo = (collisionFilterInfo & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kLOS);

    // Perform raycast
    if (bhkWorld->PickObject(ray) && ray.rayOutput.HasHit())
    {
        float fraction = std::clamp(ray.rayOutput.hitFraction, 0.0f, 1.0f);
        RE::NiPoint3 delta = rayUnscaledTo - rayUnscaledFrom;
        RE::NiPoint3 hitPos = rayUnscaledFrom + delta * fraction;

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
        if (hitPos.z > playerPos.z - maxStepUpHeight)
        {
            logger::debug("Hit surface is above playerPos.z — likely a wall.");
            return false;
        }

        float verticalDrop = playerPos.z - hitPos.z;
        logger::trace("Ledge drop at {:.2f} units ahead: {:.2f} units down", ledgeDistance, verticalDrop);

        if (verticalDrop > dropThreshold)
        {
            logger::debug("Ledge detected!");
            return true;
        }
    }
    else
    {
        logger::debug("No surface hit ahead — definite ledge.");
        return true;
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
        // logger::trace("Payload: {}", event->payload);
        // logger::trace("Tag: {}\n", event->tag);
        if (!isAttacking && (event->tag == "PowerAttack_Start_end" || event->tag == "MCO_DodgeInitiate" || event->tag == "RollTrigger"))
        {
            isAttacking = true;
            logger::debug("Animation Started");
        }
        else if (isAttacking && (event->tag == "attackStop" || event->tag == "MCO_DodgeOpen" || event->tag == "RollStop"))
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
        logger::debug("Cliff detected");
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
    spdlog::set_level(spdlog::level::debug);

    logger::info("Attack Animation Fling Fix NG Plugin Starting");

    auto *messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener("SKSE", MessageHandler);

    logger::info("Attack Animation Fling Fix NG Plugin Loaded");

    return true;
}
