module;


#include <EABase/eabase.h>
#include <EASTL/expected.h>
#include <EASTL/map.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <filesystem>
#include <mutex>
#include <print>
#include <scriptbuilder.h>

#include <fstream>

#include <angelscript.h>
#include "scriptpromise/aspromise.hpp"


export module AngelEngine.ModuleLoader;

import AngelEngine.Interfaces;
import AngelEngine.Logger;

namespace fs = std::filesystem;

namespace AngelEngine
{
    export class ModuleLoader final : public IModuleLoader
    {
    public:
        using PtrType = eastl::unique_ptr<ModuleLoader>;

        explicit ModuleLoader(eastl::unique_ptr<IScriptSourceProvider> provider) : provider_(eastl::move(provider))
        {
            builder_ = eastl::make_unique<CScriptBuilder>();
        }

        const eastl::vector<eastl::string>& GetLoadedModules() const override { return loaded_modules_; }

        bool Empty() const override { return loaded_modules_.empty(); }

        const eastl::vector<eastl::string>& GetSaveableVars(const eastl::string& modName) const override
        {
            // Здесь мы добавили лок в прошлом шаге для потокобезопасности
            std::scoped_lock lock(mutex_);
            auto it = saveable_vars_cache_.find(modName);
            if (it != saveable_vars_cache_.end())
            {
                return it->second;
            }
            static const eastl::vector<eastl::string> empty;
            return empty;
        }

        void RecordCompilationError(const eastl::string& sectionName) override
        {
            std::scoped_lock lock(mutex_);
            // The section name is formatted as "ModName:FilePath"
            auto colonPos = sectionName.find(':');
            if (colonPos != eastl::string::npos)
            {
                eastl::string modName = sectionName.substr(0, colonPos);
                if (eastl::find(faulty_modules_.begin(), faulty_modules_.end(), modName) == faulty_modules_.end())
                {
                    faulty_modules_.push_back(modName);
                    Log::Error("[ScriptEngine] Recorded faulty module: {}", modName.c_str());
                }
            }
        }

        eastl::expected<void, ModuleLoaderError>
        CompileAllMods(asIScriptEngine* engine, const eastl::vector<ChannelDescriptor>& eventDescriptors = {}) override
        {
            std::scoped_lock lock(mutex_);
            auto localDescriptors = eventDescriptors; // Copy to avoid potential reference issues during retries

            eastl::vector<eastl::string> availableMods = provider_->GetAvailableMods();
            if (availableMods.empty())
            {
                Log::Error("[ScriptEngine] No mods found.");
                return eastl::unexpected(ModuleLoaderError::PathNotFoundError);
            }

            // Retry loop for Megamodule compilation
            bool compiled = false;
            while (!compiled && !availableMods.empty())
            {
                loaded_modules_.clear();
                saveable_vars_cache_.clear();

                builder_ = eastl::make_unique<CScriptBuilder>();
                int r = builder_->StartNewModule(engine, MegaModuleName);
                if (r < 0)
                {
                    Log::Error("[ScriptEngine] Failed to create __Megamodule__ with AS code: {}", r);
                    return eastl::unexpected(ModuleLoaderError::CreateModuleError);
                }

                // Inject the AS dispatcher section FIRST so mods can call Subscribe* in their main()
                if (!localDescriptors.empty())
                {
                    eastl::string dispatcherCode = GenerateDispatcherCode(localDescriptors);
                    int dr = builder_->AddSectionFromMemory("__dispatcher__", dispatcherCode.c_str(),
                                                            static_cast<unsigned int>(dispatcherCode.size()), 0);
                    if (dr < 0)
                        Log::Error("[ModuleLoader] Failed to add __dispatcher__ section: {}", dr);
                }

                for (const auto& modName : availableMods)
                {
                    auto loadModResult = LoadScriptsFromProvider(modName);
                    if (!loadModResult)
                    {
                        Log::Error("[ScriptEngine] Failed to load scripts for: {}", modName.c_str());
                        // If a mod fails to load, we could remove it and retry, but for now we just return error.
                        return loadModResult;
                    }
                    loaded_modules_.push_back(modName);
                }

                r = builder_->BuildModule();
                if (r < 0)
                {
                    Log::Error("[ScriptEngine] __Megamodule__ Compilation FAILED with AS code: {}", r);
                    engine->DiscardModule(MegaModuleName);

                    if (!faulty_modules_.empty())
                    {
                        Log::Error("[ScriptEngine] Rebuilding Megamodule without faulty modules...");
                        for (const auto& faultyMod : faulty_modules_)
                        {
                            availableMods.erase(eastl::remove(availableMods.begin(), availableMods.end(), faultyMod),
                                                availableMods.end());
                        }
                        faulty_modules_.clear(); // Clear for the next attempt
                        continue; // Retry while loop
                    }
                    else
                    {
                        Log::Error(
                            "[ScriptEngine] Compilation failed but no faulty module was identified from callbacks.");
                        return eastl::unexpected(ModuleLoaderError::BuildModuleError);
                    }
                }

                compiled = true;
            }

            if (!compiled)
            {
                Log::Error("[ScriptEngine] All compilation attempts failed.");
                return eastl::unexpected(ModuleLoaderError::BuildModuleError);
            }

            // Parse metadata for saveable variables
            saveable_vars_cache_.clear();
            asIScriptModule* mod = builder_->GetModule();
            if (mod)
            {
                int globalVarCount = mod->GetGlobalVarCount();
                for (int i = 0; i < globalVarCount; ++i)
                {
                    std::vector<std::string> metadataList = builder_->GetMetadataForVar(i);

                    bool isSaveable = false;
                    for (const auto& meta : metadataList)
                    {
                        if (meta == "Save")
                        {
                            isSaveable = true;
                            break;
                        }
                    }

                    if (isSaveable)
                    {
                        const char* varName = nullptr;
                        const char* nameSpace = nullptr;
                        mod->GetGlobalVar(i, &varName, &nameSpace);
                        if (varName)
                        {
                            // saveable_vars_cache_ is keyed by modName, which corresponds to the namespace.
                            // If nameSpace is empty, it's a global var (maybe from a public_api mod).
                            eastl::string modKey =
                                nameSpace && nameSpace[0] != '\0' ? nameSpace : loaded_modules_.front();
                            saveable_vars_cache_[modKey].push_back(varName);
                        }
                    }
                }
            }
            else
            {
                Log::Error("[ScriptEngine] Failed to retrieve __Megamodule__ after build");
                return eastl::unexpected(ModuleLoaderError::BuildModuleError);
            }

            Log::Info("[ScriptEngine] + Successfully loaded __Megamodule__ with {} mods", loaded_modules_.size());
            return {};
        }

    private:
        // -----------------------------------------------------------------
        // Generate the AS-side dispatcher code from channel descriptors.
        // The generated section provides:
        //   - funcdef for each event type
        //   - array<Callback@> + array<bool> (for SubscribeOnce)
        //   - Subscribe / SubscribeOnce / Unsubscribe functions
        //   - __EngineDispatch<Event>__() function called by C++ (1 crossing)
        // -----------------------------------------------------------------
        static eastl::string GenerateDispatcherCode(const eastl::vector<ChannelDescriptor>& descriptors)
        {
            eastl::string code;
            code += "// AUTO-GENERATED DISPATCHER - DO NOT EDIT\n";

            for (const auto& d : descriptors)
            {
                const eastl::string dispatchFn = "__EngineDispatch" + d.eventName + "__";

                // 1. funcdef (only if provided, otherwise assume bound in C++)
                if (!d.funcdefDecl.empty())
                {
                    code += d.funcdefDecl.c_str();
                    code += ";\n";
                }

                // 2. subscriber array + once-flags
                code += "array<" + d.callbackType + "@> __" + d.eventName + "Subs;\n";
                code += "array<bool> __" + d.eventName + "Once;\n";

                // 3. engine dispatcher function
                if (d.isDeferred)
                {
                    if (d.argTypes.empty())
                    {
                        code += "void " + dispatchFn + "(uint eventCount)\n{\n";
                        code += "    uint len;\n";
                        code += "    for (uint e = 0; e < eventCount; e++) {\n";
                    }
                    else
                    {
                        code += "void " + dispatchFn + "(";
                        for (size_t k = 0; k < d.argTypes.size(); ++k)
                        {
                            code += "NativeViewArray<" + d.argTypes[k] + ">& " + d.argNames[k];
                            if (k + 1 < d.argTypes.size())
                                code += ", ";
                        }
                        code += ")\n{\n";
                        if (!d.argNames.empty())
                            code += "    uint eventCount = " + d.argNames[0] + ".length();\n";
                        code += "    uint len;\n";
                        code += "    for (uint e = 0; e < eventCount; e++) {\n";
                    }

                    // Iterate backwards: if a script removes an element during its call,
                    // it only shifts elements at or after the current index, leaving lower indices intact.
                    code += "        len = __" + d.eventName + "Subs.length();\n";
                    code += "        while (len > 0)\n        {\n";
                    code += "            len--;\n";
                    code += "            if (@__" + d.eventName + "Subs[len] != null) __" + d.eventName + "Subs[len](";

                    for (size_t k = 0; k < d.argNames.size(); ++k)
                    {
                        code += d.argNames[k] + "[e]";
                        if (k + 1 < d.argNames.size())
                            code += ", ";
                    }

                    code += ");\n";
                    code += "            if (__" + d.eventName + "Once.length() > len && __" + d.eventName +
                        "Once[len]) {\n";
                    code += "                __" + d.eventName + "Subs.removeAt(len);\n";
                    code += "                __" + d.eventName + "Once.removeAt(len);\n";
                    code += "            }\n";
                    code += "        }\n";
                    code += "    }\n}\n";
                }
                else
                {
                    code += "void " + dispatchFn + "(";
                    code += d.argDecl;
                    code += ")\n{\n";
                    code += "    uint len = __" + d.eventName + "Subs.length();\n";
                    code += "    while (len > 0)\n    {\n";
                    code += "        len--;\n";
                    code += "        if (@__" + d.eventName + "Subs[len] != null) __" + d.eventName + "Subs[len](";
                    code += d.dispatchArgs;
                    code += ");\n";
                    code +=
                        "        if (__" + d.eventName + "Once.length() > len && __" + d.eventName + "Once[len]) {\n";
                    code += "            __" + d.eventName + "Subs.removeAt(len);\n";
                    code += "            __" + d.eventName + "Once.removeAt(len);\n";
                    code += "        }\n";
                    code += "    }\n}\n";
                }

                // 4. Subscribe / SubscribeOnce / Unsubscribe
                // If the channel is handled manually by C++ (no funcdef string), we omit creating AS helpers.
                if (!d.funcdefDecl.empty())
                {
                    code += "void Subscribe" + d.eventName + "(" + d.callbackType + "@ cb)\n{\n";
                    code += "    __" + d.eventName + "Subs.insertLast(@cb);\n";
                    code += "    __" + d.eventName + "Once.insertLast(false);\n}\n";

                    code += "void Subscribe" + d.eventName + "Once(" + d.callbackType + "@ cb)\n{\n";
                    code += "    __" + d.eventName + "Subs.insertLast(@cb);\n";
                    code += "    __" + d.eventName + "Once.insertLast(true);\n}\n";
                }

                // 5. Unsubscribe
                code += "void Unsubscribe" + d.eventName + "(" + d.callbackType + "@ cb)\n{\n";
                code += "    uint len = __" + d.eventName + "Subs.length();\n";
                code += "    while (len > 0) {\n";
                code += "        len--;\n";
                code += "        if (@__" + d.eventName + "Subs[len] is @cb) {\n";
                code += "            __" + d.eventName + "Subs.removeAt(len);\n";
                code += "            __" + d.eventName + "Once.removeAt(len);\n";
                code += "            return;\n        }\n    }\n}\n";

                code += "\n"; // blank line between channels
            }

            return code;
        }

        // Returns the AS function declaration for a channel's dispatcher function,
        // used by ScriptEngine to look it up via GetFunctionByDecl() after compile.
        static eastl::string GetDispatcherFnDecl(const ChannelDescriptor& d)
        {
            if (d.isDeferred)
            {
                if (d.argTypes.empty())
                    return "void __EngineDispatch" + d.eventName + "__(" + "uint" + ")";

                eastl::string decl = "void __EngineDispatch" + d.eventName + "__(";
                for (size_t k = 0; k < d.argTypes.size(); ++k)
                {
                    decl += "array<" + d.argTypes[k] + ">@";
                    if (k + 1 < d.argTypes.size())
                        decl += ", ";
                }
                decl += ")";
                return decl;
            }
            return "void __EngineDispatch" + d.eventName + "__(" + d.argDecl + ")";
        }
        eastl::expected<void, ModuleLoaderError> LoadScriptsFromProvider(const eastl::string& modName) const
        {
            auto modPath = provider_->GetModPath(modName);
            auto scripts = provider_->GetScriptFiles(modPath);
            if (scripts.empty())
                return eastl::unexpected(ModuleLoaderError::LoadScriptError);

            for (const auto& scriptPath : scripts)
            {
                // 1. Читаем исходный код скрипта из файла в память
                std::ifstream file(scriptPath, std::ios::binary | std::ios::ate);
                if (!file.is_open())
                {
                    Log::Error("[ScriptEngine] Failed to open script file: {}", scriptPath.string().c_str());
                    return eastl::unexpected(ModuleLoaderError::LoadScriptError);
                }

                auto fileSize = file.tellg();
                file.seekg(0, std::ios::beg);

                eastl::string code;
                code.resize(fileSize);
                if (fileSize > 0)
                {
                    file.read(code.data(), fileSize);
                }

                // Wrap the code in `namespace ModName { ... }` unconditionally
                eastl::string wrappedCode = "namespace ";
                wrappedCode += modName;
                wrappedCode += "\n{\n";
                wrappedCode += code;
                wrappedCode += "\n}\n";
                code = wrappedCode;

                // 2. Препроцессинг PROMISE (магия co_await)
                size_t codeSize = code.size();

                // Функция AsGeneratePromiseEntrypoints выделит новую память под измененный код.
                // Так как мы уже переопределили глобальные asAllocMem/asFreeMem на mimalloc в ScriptEngine,
                // мы можем безопасно использовать дефолтные параметры аллокатора в функции.
                char* processedCode = AsGeneratePromiseEntrypoints(code.c_str(), &codeSize);

                // 3. Загружаем обработанный код в CScriptBuilder.
                // Мы передаем scriptPath как имя секции, чтобы ошибки компиляции AS указывали на реальный файл.
                eastl::string sectionName = modName + ":" + scriptPath.string().c_str();
                int r = builder_->AddSectionFromMemory(sectionName.c_str(), processedCode,
                                                       static_cast<unsigned int>(codeSize), 0);

                // 4. Обязательно освобождаем память, выделенную генератором
                asFreeMem(processedCode);

                if (r < 0)
                {
                    Log::Error("[ScriptEngine] Failed to add file: {} with AS code: {}", scriptPath.string().c_str(),
                               r);
                    return eastl::unexpected(ModuleLoaderError::LoadScriptError);
                }
            }
            return {};
        }

        mutable std::recursive_mutex mutex_;
        eastl::vector<eastl::string> loaded_modules_;
        eastl::vector<eastl::string> faulty_modules_;
        mutable eastl::map<eastl::string, eastl::vector<eastl::string>> saveable_vars_cache_;
        eastl::unique_ptr<CScriptBuilder> builder_;
        eastl::unique_ptr<IScriptSourceProvider> provider_;
    };
} // namespace AngelEngine
