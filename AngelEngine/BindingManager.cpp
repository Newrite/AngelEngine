module;

#include <EABase/eabase.h>
#include <EASTL/expected.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <asbind20/asbind.hpp>
#include <angelscript.h>
#include <print>
#include <string>

#include <scripteastlstring/scripteastlstring.h>
#include <scriptarray.h>
#include <scriptedictionary/scriptdictionary.h>
#include <scriptmath.h>
#include <scriptfile.h>
#include <scriptany.h>
#include <datetime.h>
#include <scripthandle.h>
#include <weakref.h>

export module AngelEngine.BindingManager;

import AngelEngine.Interfaces;
import AngelEngine.EventsBinding;

namespace AngelEngine
{
    export class BindingManager final : public IBindingManager
    {
    public:
        void RegisterStandardAddons(asIScriptEngine* engine) override
        {
            RegisterEASTLString(engine);
            RegisterScriptArray(engine, true);
            RegisterScriptDictionary(engine);
            RegisterScriptMath(engine);
            RegisterScriptFile(engine);
            RegisterScriptAny(engine);
            RegisterScriptDateTime(engine);
            RegisterScriptHandle(engine);
            RegisterScriptWeakRef(engine);
            RegisterEASTLStringUtils(engine);
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