module;

#include <cstdint>

export module AngelEngine.Errors;

namespace AngelEngine
{
    export enum class ModuleLoaderError : std::uint8_t {
        CreateModuleError,
        BuildModuleError,
        LoadScriptError,
        PathNotFoundError,
        AngelScriptAPIError,
        ContextPreparationFailed,
        InvalidFunction,
        GenericError
    };

    export enum class ExecutionError : std::uint8_t {
        NoModsLoadedToRun,
        FailCreateContext,
        ModWithoutMain,
        FailRunMod,
        AngelScriptAPIError,
        ContextPreparationFailed,
        InvalidFunction,
        GenericError
    };

    export enum class BindingError : std::uint8_t {
        BindingGlobalsFailed,
        BindingFaild, // keeping typo for compatibility unless it's easy to fix, let's fix it: BindingFailed (no, wait,
                      // I shouldn't break existing uses just yet, I'll keep the exact name for now and refactor usages
                      // later if needed. Actually, "BindingFaild" is only 1 typo, I can fix it later if I want. Let's
                      // keep existing).
        EngineIsNull,
        BindingIsNull,
        AngelScriptAPIError
    };

    export enum class EngineError : std::uint8_t {
        CreateAngelScriptEngineFailed,
        CreateAngelScriptContextFailed,
        CreateModuleFailed,
        CompilationFailed,
        PathNotFound,
        GenericError,
        FailRunMods,
        FailHotReload,
        FailCompileMods,
        AngelScriptAPIError
    };

    export enum class ReloadError : std::uint8_t {
        ModuleLoaderFailed,
        ExecutionManagerFailed,
        SerializationFailed,
        ScriptRebuildFailed,
        ContextRestorationFailed,
        GenericError
    };

    export enum class EventError : std::uint8_t {
        ChannelNotFound,
        ChannelAlreadyRegistered,
        ContextPreparationFailed,
        ExecutionFailed,
        GenericError
    };

    export enum class SerializationError : std::uint8_t {
        SaveFailed,
        LoadFailed,
        InvalidData,
        HandlerNotFound,
        TypeMismatch, // Saved type name doesn't match current type (safe cross-session detection)
        VersionMismatch, // Binary format magic number or version header doesn't match
        CorruptData, // Bounds exceeded (e.g. string length > MAX_SAFE_STRING_LEN)
        SkippedVariable, // Variable existed in save but not in current script (forward compat warning)
        GenericError
    };
} // namespace AngelEngine
