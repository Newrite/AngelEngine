set_xmakever("3.0.5")

set_project("AngelScriptPlayground")
set_version("1.0.0")

set_policy("package.requires_lock", true)
set_policy("check.auto_ignore_flags", false)
-- AngelScript пока не использует модули C++, но для вашего кода оставим
set_policy("build.c++.modules", true)
set_policy("build.optimization.lto", true)

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

set_warnings("all", "error")

-- Используем последний стандарт, asbind20 требует минимум C++20
set_languages("c++latest")

set_toolchains("clang-cl")

set_arch("x64")
-- add_cxflags("/arch:AVX2")

-- Глобальные настройки оптимизации для Release
if is_mode("release") then
    set_optimize("fastest")
    add_cxflags("/Ob2", "/Oi", "/Ot")
end

add_requires("doctest")
add_requires("eastl")

-------------------------------------------------------------------------------
-- Target: AngelScript Static Library
-------------------------------------------------------------------------------
target("angelscript_lib")
    set_kind("static")
    set_warnings("none") -- Отключаем варнинги для чужого кода, чтобы не ломать билд

    local as_root = "angelscript/sdk/angelscript"
    local as_addon = "angelscript/sdk/add_on"

    -- Core AngelScript
    add_files(as_root .. "/source/*.cpp")
    add_files(as_root .. "/source/as_callfunc_x64_msvc_asm.asm")
    add_includedirs(as_root .. "/include", {public = true})

    -- 1. Строки (String)
    add_files(as_addon .. "/scriptstdstring/*.cpp")
    add_includedirs(as_addon .. "/scriptstdstring", {public = true})

    -- 2. Массивы (Array)
    add_files(as_addon .. "/scriptarray/*.cpp")
    add_includedirs(as_addon .. "/scriptarray", {public = true})

    -- 3. Словари (Dictionary)
    add_files(as_addon .. "/scriptdictionary/*.cpp")
    add_includedirs(as_addon .. "/scriptdictionary", {public = true})

    -- 4. Математика (Math)
    add_files(as_addon .. "/scriptmath/*.cpp")
    add_includedirs(as_addon .. "/scriptmath", {public = true})

    -- 5. Файловая система (опционально, полезно для тестов)
    add_files(as_addon .. "/scriptfile/*.cpp")
    add_includedirs(as_addon .. "/scriptfile", {public = true})

    -- 6. scriptbuilder
    add_files(as_addon .. "/scriptbuilder/*.cpp")
    add_includedirs(as_addon .. "/scriptbuilder", {public = true})

    -- 7. serializer
    add_files(as_addon ..  "/serializer/*.cpp")
    add_includedirs(as_addon .. "/serializer", {public = true})

    -- 8. any
    add_files(as_addon ..  "/scriptany/*.cpp")
    add_includedirs(as_addon .. "/scriptany", {public = true})

    add_files(as_addon ..  "/contextmgr/*.cpp")
    add_includedirs(as_addon .. "/contextmgr", {public = true})

    add_files(as_addon ..  "/datetime/*.cpp")
    add_includedirs(as_addon .. "/datetime", {public = true})

    add_files(as_addon ..  "/scripthandle/*.cpp")
    add_includedirs(as_addon .. "/scripthandle", {public = true})

    add_files(as_addon ..  "/scripthelper/*.cpp")
    add_includedirs(as_addon .. "/scripthelper", {public = true})

    add_files(as_addon ..  "/weakref/*.cpp")
    add_includedirs(as_addon .. "/weakref", {public = true})

    add_files(as_addon ..  "/debugger/*.cpp")
    add_includedirs(as_addon .. "/debugger", {public = true})

    -- Дефайны для отключения исключений в самом AS (если нужно)
    -- AS_NO_EXCEPTIONS уберет try-catch внутри AS, но C++ исключения всё равно могут летать
    add_defines("AS_NO_EXCEPTIONS")

    add_cxflags(
        "-Wno-deprecated-declarations",
        {tools = "clang_cl"}
    )

-------------------------------------------------------------------------------
-- Target: JIT Library
-------------------------------------------------------------------------------
target("as_jit")
    set_kind("static")
    set_warnings("none")

    local jit_root = "AngelScript-JIT-Compiler" -- Путь к склонированному репо
    local as_root = "angelscript/sdk/angelscript"

    add_files(jit_root .. "/*.cpp")

    -- Исключаем несовместимые файлы
        if is_plat("windows") then
            remove_files(jit_root .. "/virtual_asm_linux.cpp")
        else
            remove_files(jit_root .. "/virtual_asm_windows.cpp")
        end

        if is_arch("x64") then
            remove_files(jit_root .. "/virtual_asm_x86.cpp")
        else
            remove_files(jit_root .. "/virtual_asm_x64.cpp")
        end

    add_includedirs(jit_root, {public = true})

    -- Важно: JIT должен знать путь к заголовкам самого AngelScript
    add_includedirs(as_root .. "/source")
    add_includedirs(as_root .. "/include")

    -- JIT использует некоторые дефайны для определения платформы,
    -- но xmake и исходники обычно сами справляются (AS_JIT_AMD64 определяется автоматически)

target("AngelEngine")
    set_kind("static")

    add_packages("eastl", {public = true})
    add_defines("EASTL_OPENSOURCE")
    add_defines("AS_PROCESS_METADATA=1")
    -- add_defines("EASTL_USER_DEFINED_ALLOCATOR=0")

    add_files("AngelEngine/*.cpp", {public = true})
    -- Точечно глушим варнинг для всего аддона строк (чтобы не вылезло в других файлах аддона)
    add_files("AngelEngine/Addons/**.cpp", {
        cxflags = "-Wno-unused-but-set-variable"
    })
    add_headerfiles("AngelEngine/**.h", {public = true})

    -- Подключаем asbind20 (header-only библиотека, но указываем include)
    add_includedirs("asbind20/include")
    add_includedirs("AngelEngine/Addons")

    add_deps("angelscript_lib")
    add_deps("as_jit")

    add_ldflags("-fuse-ld=lld", { tools = "clang-cl" })
    add_cxflags("/utf-8")

    add_cxflags(
        "-Wno-unused-command-line-argument",
        {tools = "clang_cl"}
    )

    -- Разрешаем использование std::print и других новых фич
    add_defines("_CRT_SECURE_NO_WARNINGS")

-------------------------------------------------------------------------------
-- Target: Main Executable
-------------------------------------------------------------------------------
target("AngelScriptPlayground")
    set_kind("binary")

    add_files("src/**.cpp")
    add_headerfiles("src/TestContext.hpp")
    add_deps("AngelEngine")

    add_includedirs("asbind20/include")
    add_includedirs("src")

    add_packages("eastl", {public = true})
    add_defines("EASTL_OPENSOURCE")

    add_ldflags("-fuse-ld=lld", { tools = "clang-cl" })
    add_cxflags("/utf-8")

    add_cxflags(
        "-Wno-unused-command-line-argument",
        {tools = "clang_cl"}
    )

    -- Разрешаем использование std::print и других новых фич
    add_defines("_CRT_SECURE_NO_WARNINGS")

    set_rundir("$(projectdir)")
