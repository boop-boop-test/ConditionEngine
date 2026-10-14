cmake_minimum_required(VERSION 3.21)
project(
    ConditionEngine
    VERSION 1.1.0
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

# CommonLibSSE: when consumed by DressCode, the parent already built this.
# Standalone builds use extern/CommonLibVR-ng, falling back to a sibling checkout
# in the parent repository so a second clone is not required.
if(NOT TARGET CommonLibSSE::CommonLibSSE)
    set(CONDITIONENGINE_COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibVR-ng"
        CACHE PATH "Path to a CommonLibVR-ng checkout")
    if(NOT EXISTS "${CONDITIONENGINE_COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")
        set(CONDITIONENGINE_COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../extern/CommonLibVR-ng"
            CACHE PATH "Path to a CommonLibVR-ng checkout" FORCE)
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
    include/ConditionEngine/Export.h
    include/ConditionEngine/Expression.h
    include/ConditionEngine/Forms.h
    include/ConditionEngine/Presets.h
    include/ConditionEngine/Registry.h
    include/ConditionEngine/Schema.h
    include/ConditionEngine/Watcher.h
    include/ConditionEngine/Conditions/ActorConditions.h
    include/ConditionEngine/Conditions/ActorStateConditions.h
    include/ConditionEngine/Conditions/ActorValueConditions.h
    include/ConditionEngine/Conditions/CallbackCondition.h
    include/ConditionEngine/Conditions/ConstantCondition.h
    include/ConditionEngine/Conditions/EquipmentConditions.h
    include/ConditionEngine/Conditions/FactionConditions.h
    include/ConditionEngine/Conditions/GameCondition.h
    include/ConditionEngine/Conditions/GearConditions.h
    include/ConditionEngine/Conditions/GlobalVariableConditions.h
    include/ConditionEngine/Conditions/LocationConditions.h
    include/ConditionEngine/Conditions/LogicalConditions.h
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
    include/ConditionEngine/Settings.h
)

set(CONDITION_ENGINE_SOURCES
    src/Availability.cpp
    src/BuiltinTypes.cpp
    src/Condition.cpp
    src/EvaluationContext.cpp
    src/Expression.cpp
    src/Forms.cpp
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
)

# DLL-only: exported C API, Papyrus natives and the SKSE plugin entry point.
set(CONDITION_ENGINE_DLL_SOURCES
    src/Api.cpp
    src/Papyrus.cpp
    src/Plugin.cpp
)

# When consumed by a parent project (DressCode), build as static lib.
# When built standalone (top-level project), build ConditionEngine.dll as an SKSE plugin.
if(PROJECT_IS_TOP_LEVEL)
    add_commonlibsse_plugin(condition-engine-lib
        NAME "ConditionEngine"
        AUTHOR "DressCode"
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
    add_executable(condition-engine-tests tests/ExpressionTests.cpp ${CONDITION_ENGINE_SOURCES})
    target_compile_definitions(condition-engine-tests PRIVATE CONDITIONENGINE_STATIC)
    target_include_directories(condition-engine-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
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
