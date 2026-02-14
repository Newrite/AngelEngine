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

    export class EventBinding final : public IScriptBinding
    {
    public:
        explicit EventBinding(IEventManager* eventManager) : eventManager_(eventManager) {}

        void Bind(asIScriptEngine* engine) override
        {
            // Регистрируем глобальную функцию подписки. 
            // Она принимает строку (название эвента) и ЛЮБУЮ функцию (asIScriptFunction@).
            
            // Здесь asbind20 не очень подходит, потому что он работает со строгой типизацией C++.
            // Поэтому используем сырой API для этой конкретной функции.
            
            engine->RegisterGlobalFunction(
                "void Subscribe(const string &in eventName, asIScriptFunction@ callback)",
                asFUNCTION(ProxySubscribe),
                asCALL_CDECL
            );
            
            // Сохраняем указатель глобально для прокси-функции (хак для CDECL)
            // В идеале использовать asCALL_THISCALL_ASGLOBAL или передавать через UserData движка.
            GlobalEventManager = eventManager_; 
        }

    private:
        static void ProxySubscribe(std::string* eventName, asIScriptFunction* callback)
        {
            if (GlobalEventManager && eventName && callback)
            {
                // Конвертируем std::string (от аддона scriptstdstring) в eastl::string
                GlobalEventManager->Subscribe(eastl::string(eventName->c_str()), callback);
            }
        }
        
        static inline IEventManager* GlobalEventManager = nullptr;
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
