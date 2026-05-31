-- include subprojects
includes("lib/commonlibf4")

-- add dependencies
add_requires("nlohmann_json")

-- set project constants
set_project("lootman")
set_version("3.1.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- define targets
target("lootman")
    add_rules("commonlibf4.plugin", {
        name = "lootman",
        author = "eideehi",
        description = "This is the F4SE plugin used in the 'LootMan' mod for Fallout 4."
    })

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- add packages
    add_packages("nlohmann_json")
