module;

#include <EABase/eabase.h>
#include <EASTL/expected.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <asbind20/asbind.hpp>
#include <angelscript.h>
#include <print>
#include <string>

#include <scriptstdstring.h>
#include <scriptarray.h>
#include <scriptdictionary.h>
#include <scriptmath.h>
#include <scriptfile.h>
#include <scriptany.h>
#include <datetime.h>
#include <scripthandle.h>
#include <weakref.h>

export module AngelEngine.BindingManager;

import AngelEngine.Interfaces;

namespace AngelEngine
{
    
    IEventManager* eventManager;

    export class EventBinding final : public IScriptBinding
    {
    public:
        explicit EventBinding(IEventManager* eventManager) : eventManager_(eventManager)
        {
            eventManager = eventManager_;
        }

        void Bind(asIScriptEngine* engine) override
        {
            // Регистрируем глобальную функцию подписки. 
            // Она принимает строку (название эвента) и ЛЮБУЮ функцию (asIScriptFunction@).
            
            // Используем asCALL_CDECL_OBJLAST, чтобы передать 'this' (EventBinding*) как последний параметр.
            // Это позволяет избежать использования глобальной статической переменной.
            
            int r = engine->RegisterGlobalFunction(
                            "void Subscribe(const string &in eventName, ?&in callback)",
                            asFUNCTION(ProxySubscribe),
                            asCALL_CDECL
                        );

            if (r < 0)
            {
                std::println(stderr, "[EventBinding] Failed to register Subscribe function. Code: {}", r);
            }
        }

    private:
        // [FIX] Исправлена сигнатура C++ функции для соответствия asCALL_CDECL и типам AS
        static void ProxySubscribe(const std::string& eventName, void* callbackRef, int typeId)
        {
            if (!eventManager) return;
            if (!callbackRef) return;

            // Получаем информацию о типе переданного аргумента
            asIScriptContext* ctx = asGetActiveContext();
            if (!ctx) return;
            
            asIScriptEngine* engine = ctx->GetEngine();
            asITypeInfo* type = engine->GetTypeInfoById(typeId);

            // Проверяем, что переданный объект является funcdef (делегатом функции)
            if (type && (type->GetFlags() & asOBJ_FUNCDEF))
            {
                // Безопасный каст, так как funcdef в AS представлен как asIScriptFunction*
                asIScriptFunction* func = static_cast<asIScriptFunction*>(callbackRef);
                
                eventManager->Subscribe(eastl::string(eventName.c_str()), func);
            }
            else
            {
                std::println(stderr, "[ScriptEngine] Error: Subscribe called with invalid type for event '{}'. Expected a function handle.", eventName);
            }
        }
        
        IEventManager* eventManager_;
    };

    export class BindingManager final : public IBindingManager
    {
    public:
        void RegisterStandardAddons(asIScriptEngine* engine) override
        {
            RegisterStdString(engine);
            RegisterScriptArray(engine, true);
            RegisterScriptDictionary(engine);
            RegisterScriptMath(engine);
            RegisterScriptFile(engine);
            RegisterScriptAny(engine);
            RegisterScriptDateTime(engine);
            RegisterScriptHandle(engine);
            RegisterScriptWeakRef(engine);
            RegisterStdStringUtils(engine);
        }
        
        eastl::expected<void, BindingError> BindAll(asIScriptEngine* const engine) override
        {
            
            if (!engine)
            {
                return eastl::unexpected(BindingError::EngineIsNull);
            }
            
            try
            {
                
                for (auto* binding : bindings_)
                {
                    auto result = this->Bind(engine, binding);
                    if (!result)
                    {
                        std::println("Binding error: {}", static_cast<int>(result.error()));
                    }   
                }
            }
            catch (const std::exception& e)
            {
                std::println("Binding error: {}", e.what());
                return eastl::unexpected(BindingError::BindingGlobalsFailed);
            }

            return {};
        }
        
        eastl::expected<void, BindingError> Bind(asIScriptEngine* const engine, IScriptBinding* binding) override
        {
            if (!engine)
            {
                return eastl::unexpected(BindingError::EngineIsNull);
            }
            
            if (!binding)
            {
                return eastl::unexpected(BindingError::BindingIsNull);
            }
            
            try
            {
                binding->Bind(engine);
            }
            catch (const std::exception& e)
            {
                std::println("Binding error: {}", e.what());
                return eastl::unexpected(BindingError::BindingGlobalsFailed);
            }
            
            return {};
            
        }

        void AddBinding(IScriptBinding* binding) override
        {
            if (!binding)
            {
                return;
            }
            bindings_.push_back(binding);
        }

    private:
        eastl::vector<IScriptBinding*> bindings_;
    };
}
