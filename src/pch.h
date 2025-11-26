#pragma once

#define NOMINMAX
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#include "SimpleIni.h"
#include <spdlog/sinks/basic_file_sink.h>
namespace logger = SKSE::log;
#include <vector>
#include "Globals.h"
#include "Config.h"
#include "Events.h"
#include "MathUtils.h"
#include "Objects.h"
#include "Utils.h"
#include "Hook.h"

#define DLLEXPORT __declspec(dllexport)

using namespace std::literals;