module;

#include <EASTL\vector.h>
#include <cassert>
#include <cstdio> // Для надежного C-style FILE*
#include <filesystem>
#include <format>
#include <string>


#include "angelscript.h"

export module AngelEngine.PredefinedGenerator;

import AngelEngine.Logger;
import AngelEngine.Interfaces;

namespace AngelEngine
{
    void printEnumList(const asIScriptEngine* engine, std::string& out)
    {
        for (int i = 0; i < engine->GetEnumCount(); i++)
        {
            const auto e = engine->GetEnumByIndex(i);
            if (not e)
                continue;
            const std::string_view ns = e->GetNamespace();
            if (not ns.empty())
                out += std::format("namespace {} {{\n", ns);
            out += std::format("enum {} {{\n", e->GetName());
            for (int j = 0; j < e->GetEnumValueCount(); ++j)
            {
                out += std::format("\t{}", e->GetEnumValueByIndex(j, nullptr));
                if (j < e->GetEnumValueCount() - 1)
                    out += ",";
                out += "\n";
            }
            out += "}\n";
            if (not ns.empty())
                out += "}\n";
        }
    }

    void printClassTypeList(const asIScriptEngine* engine, std::string& out)
    {
        for (int i = 0; i < engine->GetObjectTypeCount(); i++)
        {
            const auto t = engine->GetObjectTypeByIndex(i);
            if (not t)
                continue;

            const std::string_view ns = t->GetNamespace();
            if (not ns.empty())
                out += std::format("namespace {} {{\n", ns);

            out += std::format("class {}", t->GetName());
            if (t->GetSubTypeCount() > 0)
            {
                out += "<";
                for (int sub = 0; sub < t->GetSubTypeCount(); ++sub)
                {
                    if (sub < t->GetSubTypeCount() - 1)
                        out += ", ";
                    const auto st = t->GetSubType(sub);
                    out += st->GetName();
                }

                out += ">";
            }

            out += "{\n";
            for (int j = 0; j < t->GetBehaviourCount(); ++j)
            {
                asEBehaviours behaviours;
                const auto f = t->GetBehaviourByIndex(j, &behaviours);
                if (behaviours == asBEHAVE_CONSTRUCT || behaviours == asBEHAVE_DESTRUCT)
                {
                    out += std::format("\t{};\n", f->GetDeclaration(false, true, true));
                }
            }
            for (int j = 0; j < t->GetMethodCount(); ++j)
            {
                const auto m = t->GetMethodByIndex(j);
                out += std::format("\t{};\n", m->GetDeclaration(false, true, true));
            }
            for (int j = 0; j < t->GetPropertyCount(); ++j)
            {
                out += std::format("\t{};\n", t->GetPropertyDeclaration(j, true));
            }
            for (int j = 0; j < t->GetChildFuncdefCount(); ++j)
            {
                out +=
                    std::format("\tfuncdef {};\n", t->GetChildFuncdef(j)->GetFuncdefSignature()->GetDeclaration(false));
            }
            out += "}\n";
            if (not ns.empty())
                out += "}\n";
        }
    }

    void printGlobalFunctionList(const asIScriptEngine* engine, std::string& out)
    {
        for (int i = 0; i < engine->GetGlobalFunctionCount(); i++)
        {
            const auto f = engine->GetGlobalFunctionByIndex(i);
            if (not f)
                continue;
            const std::string_view ns = f->GetNamespace();
            if (not ns.empty())
                out += std::format("namespace {} {{ ", ns);
            out += std::format("{};", f->GetDeclaration(false, false, true));
            if (not ns.empty())
                out += " }";
            out += "\n";
        }
    }

    void printGlobalPropertyList(const asIScriptEngine* engine, std::string& out)
    {
        for (int i = 0; i < engine->GetGlobalPropertyCount(); i++)
        {
            const char* name;
            const char* ns0;
            int type;
            engine->GetGlobalPropertyByIndex(i, &name, &ns0, &type, nullptr, nullptr, nullptr, nullptr);

            const std::string t = engine->GetTypeDeclaration(type, true);
            if (t.empty())
                continue;

            std::string_view ns = ns0;
            if (not ns.empty())
                out += std::format("namespace {} {{ ", ns);

            out += std::format("{} {};", t, name);
            if (not ns.empty())
                out += " }";
            out += "\n";
        }
    }

    void printGlobalTypedef(const asIScriptEngine* engine, std::string& out)
    {
        for (int i = 0; i < engine->GetTypedefCount(); ++i)
        {
            const auto type = engine->GetTypedefByIndex(i);
            if (not type)
                continue;
            const std::string_view ns = type->GetNamespace();
            if (not ns.empty())
                out += std::format("namespace {} {{\n", ns);
            out += std::format("typedef {} {};\n", engine->GetTypeDeclaration(type->GetSubTypeId(0)), type->GetName());
            if (not ns.empty())
                out += "}\n";
        }
    }

    void printEventDispatcherAPI(const eastl::vector<ChannelDescriptor>& descriptors, std::string& out)
    {
        if (descriptors.empty())
            return;
        out += "\n// ---- Event Dispatcher API ----\n";
        for (const auto& d : descriptors)
        {
            // funcdef (callback signature)
            if (!d.funcdefDecl.empty())
            {
                out += std::format("{};\n", d.funcdefDecl.c_str());
                out += std::format("void Subscribe{}({}@ cb);\n", d.eventName.c_str(), d.callbackType.c_str());
                out += std::format("void Subscribe{}Once({}@ cb);\n", d.eventName.c_str(), d.callbackType.c_str());
            }
            out += std::format("void Unsubscribe{}({}@ cb);\n", d.eventName.c_str(), d.callbackType.c_str());
            out += "\n";
        }
    }

    /// @brief Generate 'as.predefined' file, which contains all defined symbols in C++. It is used by the language
    /// server.
    export void GenerateScriptPredefined(const asIScriptEngine* engine, const std::filesystem::path& path,
                                         const eastl::vector<ChannelDescriptor>& eventDescriptors = {})
    {
        Log::Info("[PredefinedGenerator] Attempting to generate file at: {}", std::filesystem::absolute(path).string());

        std::error_code ec;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                Log::Error("[PredefinedGenerator] Failed to create directories! Path: {}. Error: {}",
                           path.parent_path().string(), ec.message());
                // Не делаем return; возможно, директория уже существует, но заблокирована для проверки
            }
        }

        // Собираем весь текст в один буфер
        std::string out;
        out.reserve(1024 * 512); // Сразу выделяем 512 KB, чтобы избежать реаллокаций памяти

        Log::Info("[PredefinedGenerator] Generating content...");

        printEnumList(engine, out);
        printClassTypeList(engine, out);
        printGlobalFunctionList(engine, out);
        printGlobalPropertyList(engine, out);
        printGlobalTypedef(engine, out);
        printEventDispatcherAPI(eventDescriptors, out);

        Log::Info("[PredefinedGenerator] Content generated. Total size: {} bytes", out.size());

        // Используем классический C API для записи файла
        FILE* file = nullptr;
#ifdef _WIN32
        // На Windows _wfopen_s корректно работает с юникодом (русские буквы в пути и т.д.)
        _wfopen_s(&file, path.wstring().c_str(), L"wb");
#else
        file = std::fopen(path.string().c_str(), "wb");
#endif

        if (!file)
        {
            Log::Error(
                "[PredefinedGenerator] FAILED to open file for writing! Check file permissions or paths. Path: {}",
                path.string());
            return;
        }

        // Записываем весь буфер одним гигантским вызовом (максимальная производительность)
        size_t written = std::fwrite(out.data(), 1, out.size(), file);
        std::fclose(file);

        if (written != out.size())
        {
            Log::Error("[PredefinedGenerator] FAILED to write full file! Wrote {} out of {} bytes.", written,
                       out.size());
        }
        else
        {
            Log::Info("[PredefinedGenerator] SUCCESS! Predefined file written to disk.");
        }
    }
} // namespace AngelEngine
