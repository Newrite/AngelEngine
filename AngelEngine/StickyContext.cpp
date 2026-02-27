module;

#include <angelscript.h>

export module AngelEngine.StickyContext;

import AngelEngine.Interfaces; // Provides IContextPooling

namespace AngelEngine
{
    // -----------------------------------------------------------------------
    // StickyContext
    //
    // RAII wrapper for functions called on every tick (e.g. OnTick).
    //
    // NAIVE  per-call:  CAS-pop → Prepare(fn) → Execute → CAS-push
    // STICKY per-call:  Execute   (context pre-warmed, stays in our hands)
    //
    // The context is taken out of the pool at construction and returned on
    // destruction. Prepare() is called once at construction and after each
    // Execute(), so it is always ready for the next call.
    // -----------------------------------------------------------------------
    export class StickyContext
    {
    public:
        StickyContext() = default;

        StickyContext(IContextPooling* pool, asIScriptEngine* engine, asIScriptFunction* fn) :
            pool_(pool), engine_(engine), fn_(fn)
        {
            auto ctxPtr = pool_->RequestContext(engine_, nullptr);
            ctx_ = ctxPtr.release(); // steal ownership — pool will NOT auto-return
            if (ctx_)
                ctx_->Prepare(fn_); // pre-warm for first Execute()
        }

        StickyContext(const StickyContext&) = delete;
        StickyContext& operator=(const StickyContext&) = delete;

        StickyContext(StickyContext&& o) noexcept : pool_(o.pool_), engine_(o.engine_), fn_(o.fn_), ctx_(o.ctx_)
        {
            o.ctx_ = nullptr;
        }

        StickyContext& operator=(StickyContext&& o) noexcept
        {
            if (this != &o)
            {
                Release();
                pool_ = o.pool_;
                engine_ = o.engine_;
                fn_ = o.fn_;
                ctx_ = o.ctx_;
                o.ctx_ = nullptr;
            }
            return *this;
        }

        ~StickyContext() { Release(); }

        explicit operator bool() const { return ctx_ != nullptr; }

        // Run the pre-prepared function, then re-Prepare for next call.
        inline int Execute()
        {
            if (!ctx_)
                return asERROR;
            int r = ctx_->Execute();
            ctx_->Prepare(fn_); // pre-warm next call
            return r;
        }

        // Access raw context to set arguments before Execute().
        inline asIScriptContext* Get() const { return ctx_; }

        // Access engine for type info lookups
        inline asIScriptEngine* GetEngine() const { return engine_; }

    private:
        void Release()
        {
            if (ctx_ && pool_)
            {
                ctx_->Unprepare();
                pool_->ReturnContext(engine_, ctx_, nullptr);
                ctx_ = nullptr;
            }
        }

        IContextPooling* pool_ = nullptr;
        asIScriptEngine* engine_ = nullptr;
        asIScriptFunction* fn_ = nullptr;
        asIScriptContext* ctx_ = nullptr;
    };
} // namespace AngelEngine
