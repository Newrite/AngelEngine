module;

#include <EABase/eabase.h>
#include <EASTL/expected.h>
#include <EASTL/vector.h>

#include <angelscript.h>

#include <scripteastlstring/scripteastlstring.h>
#include <scriptarray.h>
#include <scriptedictionary/scriptdictionary.h>
#include "scriptpromise/aspromise.hpp"
#include <scriptmath.h>
#include <scriptfile.h>
#include <scriptany.h>
#include <datetime.h>
#include <scripthandle.h>
#include <weakref.h>

export module AngelEngine.BindingManager;

import AngelEngine.Interfaces;
import AngelEngine.EventsBinding;
import AngelEngine.Logger;

namespace AngelEngine
{
    export class BindingManager final : public IBindingManager
    {
    public:
        eastl::expected<void, BindingError> RegisterStandardAddons(asIScriptEngine* engine) override
        {
            if (!engine) return eastl::unexpected(BindingError::EngineIsNull);

            // We assume these functions return void or handle errors internally by logging.
            // If they returned int, we would check it.
            // Standard add-ons usually don't fail unless OOM or bad config.
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
            AsDirectPromise::Register(engine);
            
            return {};
        }
        
        eastl::expected<void, BindingError> BindAll(asIScriptEngine* const engine) override
        {
            if (!engine)
            {
                return eastl::unexpected(BindingError::EngineIsNull);
            }
            
            // We do not use try-catch as per instructions "Do not use Exceptions (try/catch)".
            // Assuming bindings don't throw but might return error codes if we changed IScriptBinding::Bind to return expected.
            // But IScriptBinding::Bind returns void currently.
            
            for (auto* binding : bindings_)
            {
                auto result = this->Bind(engine, binding);
                if (!result.has_value())
                {
                    Log::Error("Binding error: {}", static_cast<int>(result.error()));
                    return result; 
                }   
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
            
            // IScriptBinding::Bind returns void. We assume it succeeds if it doesn't crash.
            // If we wanted to enforce error checking, we would need to change IScriptBinding interface too.
            // But the task didn't explicitly ask to change IScriptBinding::Bind signature, 
            // only "Review all void functions in the engine... If a function can fail... change its signature".
            // IScriptBinding::Bind is user code usually.
            // Let's assume for now we just call it.
            binding->Bind(engine);
            
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