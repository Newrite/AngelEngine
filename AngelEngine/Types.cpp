module;

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <filesystem>
#include <angelscript.h>

export module AngelEngine.Types;

namespace AngelEngine
{
    
    export inline constexpr const char* MegaModuleName = "__Megamodule__";

    // AngelScript reserves IDs 1000 to 1999 for official add-ons.
    // Any custom engine user data must strictly fall outside this range.
    export enum class AsUserDataId : asPWORD {
        ExecutionPoolIndex = 2000, // Для пула контекстов
        ExecutionGeneration = 2001, // хранит эпоху в которой был создан
        CoroutinePromise = 2002, // На будущее, для асинхронщины
        WatchdogTimestamp = 2003 // Для отслеживания зависаний
    };

    // --- Configuration Structures ---
    export struct EngineConfig final
    {
        std::filesystem::path scriptsPathMod;
        std::filesystem::path asPredefinedPath;

        eastl::vector<eastl::string> enabledMods;

        bool enableAutoReload = false;
        bool enableWatchdog = true;
        bool enableAutoGC = false;
        bool enableUseJIT = false;
        int64_t maxScriptExecutionTimeMs = 1000;
    };
} // namespace AngelEngine
