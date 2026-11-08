cmake_minimum_required(VERSION 3.21)
project(
    ConditionEngine
    VERSION 1.6.0
    DESCRIPTION "Reusable condition/expression evaluator for Skyrim SKSE plugins."
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

###############################################################################
# MSVC runtime library (matches parent convention)
###############################################################################
string(CONCAT MSVC_RUNTIME_LIBRARY_STR
    $<$<CONFIG:Debug>:MultiThreadedDebugDLL>
    $<$<CONFIG:Release>:MultiThreadedDLL>)

###############################################################################
# Dependencies - guard against parent-provided targets
###############################################################################

# CommonLibSSE: when consumed by a parent project, the parent already built this.
# Standalone builds use extern/CommonLibVR-ng, falling back to a sibling checkout
# in the parent repository so a second clone is not required.
###############################################################################
# Release DLLs carry no path of the build machine: __FILE__ and std::source_location (spdlog, CommonLib's logging and
# asserts) are relative to this repo and to CommonLib's parent folder, and the debug directory names a PDB by file name
# only. Directory scope and before CommonLib is added, so its objects get it too. Standalone builds only (a parent
# that links the static library decides for its own build). /pathmap would also need /experimental:deterministic.
###############################################################################
set(CONDITIONENGINE_TRIM_PATHS OFF)
if(PROJECT_IS_TOP_LEVEL AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CONDITIONENGINE_TRIM_PATHS ON)
    add_compile_options("$<$<NOT:$<CONFIG:Debug>>:/d1trimfile:${CMAKE_CURRENT_SOURCE_DIR}/>")
    add_link_options("$<$<NOT:$<CONFIG:Debug>>:/PDBALTPATH:%_PDB%>")
endif()

if(NOT TARGET CommonLibSSE::CommonLibSSE)
    set(CONDITIONENGINE_COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibVR-ng"
        CACHE PATH "Path to a CommonLibVR-ng checkout")
    if(NOT EXISTS "${CONDITIONENGINE_COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")
        set(CONDITIONENGINE_COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/CommonLibVR-ng"
            CACHE PATH "Path to a CommonLibVR-ng checkout" FORCE)
    endif()
    if(CONDITIONENGINE_TRIM_PATHS)
        cmake_path(IS_PREFIX CMAKE_CURRENT_SOURCE_DIR "${CONDITIONENGINE_COMMONLIB_DIR}" NORMALIZE _ce_commonlib_inside)
        if(NOT _ce_commonlib_inside)
            cmake_path(GET CONDITIONENGINE_COMMONLIB_DIR PARENT_PATH _ce_commonlib_parent)
            cmake_path(ABSOLUTE_PATH _ce_commonlib_parent NORMALIZE)
            add_compile_options("$<$<NOT:$<CONFIG:Debug>>:/d1trimfile:${_ce_commonlib_parent}/>")
        endif()
    endif()
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    include("${CONDITIONENGINE_COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")
    add_subdirectory("${CONDITIONENGINE_COMMONLIB_DIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}/CommonLibVR-ng")
endif()

find_package(spdlog REQUIRED CONFIG)
find_package(nlohmann_json CONFIG QUIET)

###############################################################################
# ConditionEngine library target
###############################################################################

set(CONDITION_ENGINE_HEADERS
    include/ConditionEngine/Availability.h
    include/ConditionEngine/AllConditions.h
    include/ConditionEngine/Common.h
    include/ConditionEngine/Condition.h
    include/ConditionEngine/ConditionEngine.h
    include/ConditionEngine/ConditionEngineAPI.h
    include/ConditionEngine/EvaluationContext.h
    include/ConditionEngine/Explain.h
    include/ConditionEngine/Export.h
    include/ConditionEngine/Expression.h
    include/ConditionEngine/FirstMatchList.h
    include/ConditionEngine/Forms.h
    include/ConditionEngine/Presets.h
    include/ConditionEngine/Registry.h
    include/ConditionEngine/Schema.h
    include/ConditionEngine/Watcher.h
    include/ConditionEngine/Conditions/ActorConditions.h
    include/ConditionEngine/Conditions/ActorStateConditions.h
    include/ConditionEngine/Conditions/ActorValueConditions.h
    include/ConditionEngine/Conditions/CallbackCondition.h
    include/ConditionEngine/Conditions/CompareCondition.h
    include/ConditionEngine/Conditions/ConstantCondition.h
    include/ConditionEngine/Conditions/EquipmentConditions.h
    include/ConditionEngine/Conditions/FactionConditions.h
    include/ConditionEngine/Conditions/GameCondition.h
    include/ConditionEngine/Conditions/GearConditions.h
    include/ConditionEngine/Conditions/GlobalVariableConditions.h
    include/ConditionEngine/Conditions/LocationConditions.h
    include/ConditionEngine/Conditions/LogicalConditions.h
    include/ConditionEngine/Conditions/PapyrusCallCondition.h
    include/ConditionEngine/Conditions/QuestCategoryConditions.h
    include/ConditionEngine/Conditions/QuestConditions.h
    include/ConditionEngine/Conditions/RaceConditions.h
    include/ConditionEngine/Conditions/RandomConditions.h
    include/ConditionEngine/Conditions/RuleReferenceCondition.h
    include/ConditionEngine/Conditions/ScriptValueConditions.h
    include/ConditionEngine/Conditions/TemperatureConditions.h
    include/ConditionEngine/Conditions/TimeConditions.h
    include/ConditionEngine/Conditions/WeatherConditions.h
    include/ConditionEngine/Conditions/WorldConditions.h
    include/ConditionEngine/People.h
    include/ConditionEngine/Settings.h
    include/ConditionEngine/Stats.h
    include/ConditionEngine/Value.h
    include/ConditionEngine/ValueResult.h
    include/ConditionEngine/Values/PushedValues.h
    include/ConditionEngine/Values/RefreshOn.h
    include/ConditionEngine/Values/ValueNodes.h
    include/ConditionEngine/Gear/ArmorProtection.h
    include/ConditionEngine/Weather/WeatherProfile.h
    include/ConditionEngine/Weather/WeatherProfiles.h
)

set(CONDITION_ENGINE_SOURCES
    src/Availability.cpp
    src/BuiltinTypes.cpp
    src/Condition.cpp
    src/EvaluationContext.cpp
    src/Explain.cpp
    src/Expression.cpp
    src/FirstMatchList.cpp
    src/Forms.cpp
    src/People.cpp
    src/Presets.cpp
    src/Registry.cpp
    src/Schema.cpp
    src/Watcher.cpp
    src/Conditions/ActorConditions.cpp
    src/Conditions/ActorStateConditions.cpp
    src/Conditions/ActorValueConditions.cpp
    src/Conditions/EquipmentConditions.cpp
    src/Conditions/FactionConditions.cpp
    src/Conditions/GameCondition.cpp
    src/Conditions/GearConditions.cpp
    src/Conditions/GlobalVariableConditions.cpp
    src/Conditions/LocationConditions.cpp
    src/Conditions/LogicalConditions.cpp
    src/Conditions/PapyrusCallCondition.cpp
    src/Conditions/QuestCategoryConditions.cpp
    src/Conditions/QuestConditions.cpp
    src/Conditions/RaceConditions.cpp
    src/Conditions/RandomConditions.cpp
    src/Conditions/RuleReferenceCondition.cpp
    src/Conditions/ScriptValueConditions.cpp
    src/Conditions/TemperatureConditions.cpp
    src/Conditions/TimeConditions.cpp
    src/Conditions/WeatherConditions.cpp
    src/Conditions/WorldConditions.cpp
    src/Settings.cpp
    src/Stats.cpp
    src/Value.cpp
    src/Conditions/CompareCondition.cpp
    src/Values/PushedValues.cpp
    src/Values/RefreshOn.cpp
    src/Gear/ArmorProtection.cpp
    src/Weather/WeatherProfile.cpp
    src/Weather/WeatherProfiles.cpp
)

###############################################################################
# Built-in presets: presets/*.json compiled into the library as byte arrays
# (generated/ConditionEngine/BuiltinPresets.inc, read by src/Presets.cpp), so they ship
# with every copy of the DLL, bundled ones included. Editing a file re-runs configure.
###############################################################################
set(CONDITIONENGINE_BUILTIN_PRESETS presets/ConditionEngine_People.json presets/ConditionEngine_Survival.json
    presets/ConditionEngine_Weather.json)
set(CONDITIONENGINE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_ce_preset_arrays "")
set(_ce_preset_entries "")
foreach(_ce_preset IN LISTS CONDITIONENGINE_BUILTIN_PRESETS)
    set(_ce_path "${CMAKE_CURRENT_SOURCE_DIR}/${_ce_preset}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ce_path}")
    file(READ "${_ce_path}" _ce_hex HEX)
    string(LENGTH "${_ce_hex}" _ce_hex_length)
    get_filename_component(_ce_name "${_ce_preset}" NAME)
    string(MAKE_C_IDENTIFIER "${_ce_name}" _ce_id)
    set(_ce_bytes "")
    set(_ce_offset 0)
    while(_ce_offset LESS _ce_hex_length)
        string(SUBSTRING "${_ce_hex}" ${_ce_offset} 64 _ce_chunk)  # 32 bytes per line
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _ce_chunk "${_ce_chunk}")
        string(APPEND _ce_bytes "    ${_ce_chunk}\n")
        math(EXPR _ce_offset "${_ce_offset} + 64")
    endwhile()
    string(APPEND _ce_preset_arrays "constexpr unsigned char k_${_ce_id}[] = {\n${_ce_bytes}};\n")
    string(APPEND _ce_preset_entries "    {\"${_ce_name}\", k_${_ce_id}, sizeof(k_${_ce_id})},\n")
endforeach()
file(CONFIGURE OUTPUT "${CONDITIONENGINE_GENERATED_DIR}/ConditionEngine/BuiltinPresets.inc"
     CONTENT "// Generated by CMakeLists.txt from presets/*.json; edit those files instead.\n@_ce_preset_arrays@\nconstexpr BuiltinPreset kBuiltinPresets[] = {\n@_ce_preset_entries@};\n"
     @ONLY)

# DLL-only: exported C API, Papyrus natives, Open Animation Replacer conditions and the SKSE plugin entry point.
set(CONDITION_ENGINE_DLL_SOURCES
    src/Api.cpp
    src/Console.cpp
    src/OarConditions.cpp
    src/OAR/OpenAnimationReplacer-ConditionTypes.cpp
    src/OAR/OpenAnimationReplacerAPI-Conditions.cpp
    src/Papyrus.cpp
    src/Plugin.cpp
    # Dials, the editor and SkyrimNet (DLL only: a host that links the static library has its own)
    src/Dials/Api.cpp
    src/Dials/CEApi.cpp
    src/Dials/Console.cpp
    src/Dials/Definition.cpp
    src/Dials/Editor.cpp
    src/Dials/Engine.cpp
    src/Dials/Events.cpp
    src/Dials/GameHost.cpp
    src/Dials/OarValues.cpp
    src/Dials/Papyrus.cpp
    src/Dials/Serialization.cpp
    src/Dials/Service.cpp
    src/Dials/Settings.cpp
    src/SkyrimNet/Bridge.cpp
)

# When consumed by a parent project, build as static lib.
# When built standalone (top-level project), build ConditionEngine.dll as an SKSE plugin.
if(PROJECT_IS_TOP_LEVEL)
    add_commonlibsse_plugin(condition-engine-lib
        NAME "ConditionEngine"
        AUTHOR "boop-boop-test"
        SOURCES ${CONDITION_ENGINE_HEADERS} ${CONDITION_ENGINE_SOURCES} ${CONDITION_ENGINE_DLL_SOURCES})
    target_compile_definitions(condition-engine-lib PRIVATE CONDITIONENGINE_EXPORTS)
    set_target_properties(condition-engine-lib PROPERTIES OUTPUT_NAME "ConditionEngine")
else()
    add_library(condition-engine-lib STATIC
        ${CONDITION_ENGINE_HEADERS} ${CONDITION_ENGINE_SOURCES})
    target_compile_definitions(condition-engine-lib PUBLIC CONDITIONENGINE_STATIC)
endif()

target_include_directories(condition-engine-lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_include_directories(condition-engine-lib PRIVATE "${CONDITIONENGINE_GENERATED_DIR}")

target_link_libraries(condition-engine-lib PUBLIC
    CommonLibSSE::CommonLibSSE
    spdlog::spdlog)
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(condition-engine-lib PUBLIC nlohmann_json::nlohmann_json)
endif()

set_target_properties(condition-engine-lib PROPERTIES
    MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STR}
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON)

if(MSVC)
    target_compile_options(condition-engine-lib PRIVATE
        "/utf-8" "/Zi" "/EHsc" "/Zc:preprocessor" "/WX"
        "/experimental:external" "/external:anglebrackets" "/external:W0"
        "/wd4200")
endif()

target_precompile_headers(condition-engine-lib PRIVATE src/PCH.h)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${CONDITION_ENGINE_HEADERS} ${CONDITION_ENGINE_SOURCES})

###############################################################################
# Tests (standalone only) - expression parser/registry, runs outside the game
###############################################################################
option(CONDITIONENGINE_BUILD_TESTS "Build ConditionEngine unit tests" ${PROJECT_IS_TOP_LEVEL})
if(PROJECT_IS_TOP_LEVEL AND CONDITIONENGINE_BUILD_TESTS)
    add_executable(condition-engine-tests tests/ExpressionTests.cpp tests/ValueTests.cpp tests/WeatherTests.cpp tests/GearTests.cpp tests/PeopleTests.cpp
        tests/DialsTests.cpp tests/Benchmark.cpp src/Dials/Definition.cpp src/Dials/Engine.cpp src/Dials/OarValues.cpp ${CONDITION_ENGINE_SOURCES})
    target_compile_definitions(condition-engine-tests PRIVATE DIALS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(condition-engine-tests PRIVATE CONDITIONENGINE_STATIC)
    target_include_directories(condition-engine-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include "${CONDITIONENGINE_GENERATED_DIR}")
    target_link_libraries(condition-engine-tests PRIVATE CommonLibSSE::CommonLibSSE spdlog::spdlog)
    if(TARGET nlohmann_json::nlohmann_json)
        target_link_libraries(condition-engine-tests PRIVATE nlohmann_json::nlohmann_json)
    endif()
    set_target_properties(condition-engine-tests PROPERTIES MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STR})
    if(MSVC)
        target_compile_options(condition-engine-tests PRIVATE "/utf-8" "/EHsc" "/Zc:preprocessor" "/external:anglebrackets" "/external:W0")
    endif()
    target_precompile_headers(condition-engine-tests PRIVATE src/PCH.h)
endif()

###############################################################################
# Distribution - populate contrib/ for packaging (standalone only)
###############################################################################
if(PROJECT_IS_TOP_LEVEL)
    set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Distribution")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(BUILD_NAME "Debug")
    else()
        set(BUILD_NAME "Release")
    endif()

    add_custom_command(TARGET condition-engine-lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/Plugin${BUILD_NAME}/")
    # The editor page (Magelight UI), laid out as the installed mod
    add_custom_command(TARGET condition-engine-lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/ui/editor"
                "${DIST_DIR}/Magelight/ConditionEngine/views/editor")
    add_custom_command(TARGET condition-engine-lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:condition-engine-lib>
            "${DIST_DIR}/Plugin${BUILD_NAME}/")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_custom_command(TARGET condition-engine-lib POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_PDB_FILE:condition-engine-lib>
                "${DIST_DIR}/Plugin${BUILD_NAME}/")
    endif()
endif()
