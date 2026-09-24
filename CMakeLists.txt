cmake_minimum_required(VERSION 3.21)

# The x64-windows-static-md triplet links third-party libraries statically while
# retaining the /MD runtime required by Skyrim and CommonLibSSE-NG.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(
    SimpleTraits
    VERSION 0.1.0
    LANGUAGES CXX
)

option(ST_BUILD_PLUGIN "Build the Skyrim SKSE plugin" ON)
option(BUILD_TESTS "Build dependency-free trait, handshake, and logging tests" OFF)
set(ST_DEPLOY_DIR "" CACHE PATH "Optional MO2 mod root for post-build deployment")
set(ST_JAVA_EXECUTABLE "" CACHE FILEPATH "Java 17 executable used only by trait-menu SWF targets")
set(ST_FFDEC_JAR "" CACHE FILEPATH "FFDec 25.1.3 jar used only by trait-menu SWF targets")

if(ST_BUILD_PLUGIN)
    if(NOT WIN32)
        message(FATAL_ERROR "The SKSE plugin can only be built on Windows. Use -DST_BUILD_PLUGIN=OFF for portable tests.")
    endif()
    if(DEFINED VCPKG_TARGET_TRIPLET AND
       NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static-md")
        message(FATAL_ERROR
            "SimpleTraits requires VCPKG_TARGET_TRIPLET=x64-windows-static-md; "
            "received '${VCPKG_TARGET_TRIPLET}'.")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG/CMakeLists.txt")
        message(FATAL_ERROR
            "CommonLibSSE-NG is missing. Clone with --recurse-submodules or run "
            "'git submodule update --init --recursive'.")
    endif()

    set(SKSE_SUPPORT_XBYAK ON)
    set(ENABLE_SKYRIM_SE ON)
    set(ENABLE_SKYRIM_AE ON)
    set(ENABLE_SKYRIM_VR OFF)

    # CommonLibSSE-NG uses BUILD_TESTS for its own dependency-heavy suite.
    # Disable that suite without changing this project's public option.
    set(ST_REQUESTED_BUILD_TESTS ${BUILD_TESTS})
    set(BUILD_TESTS OFF)
    add_subdirectory(extern/CommonLibSSE-NG)
    set(BUILD_TESTS ${ST_REQUESTED_BUILD_TESTS})

    include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    set(ST_SWF_TOOL_ARGS
        --java "${ST_JAVA_EXECUTABLE}"
        --ffdec-jar "${ST_FFDEC_JAR}"
        --swf "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/ST_TraitMenu.swf"
        --scripts "${CMAKE_CURRENT_SOURCE_DIR}/assets/swf_src/scripts"
        --work-dir "${CMAKE_CURRENT_BINARY_DIR}/swf"
    )
    add_custom_target(rebuild_trait_menu
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/trait_menu_swf.py"
            rebuild ${ST_SWF_TOOL_ARGS}
        COMMENT "Rebuilding ST_TraitMenu.swf with FFDec 25.1.3"
        VERBATIM
    )
    add_custom_target(verify_trait_menu
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/trait_menu_swf.py"
            verify ${ST_SWF_TOOL_ARGS}
        COMMENT "Verifying ST_TraitMenu.swf against tracked ActionScript"
        VERBATIM
    )

    set(ST_PLUGIN_SOURCES
        src/main.cpp
        src/Config.cpp
        src/SettingsModel.cpp
        src/TraitRules.cpp
        src/TraitSave.cpp
        src/TraitState.cpp
        src/TraitMenuRules.cpp
        src/TraitMenu.cpp
        src/HandshakeRules.cpp
        src/SALBridge.cpp
        src/SettingsPage.cpp
        src/DebugPage.cpp
        src/DiagnosticSinks.cpp
        src/LogPolicy.cpp
    )

    add_commonlibsse_plugin(${PROJECT_NAME}
        NAME ${PROJECT_NAME}
        AUTHOR "EA"
        VERSION ${PROJECT_VERSION}
        USE_ADDRESS_LIBRARY
        STRUCT_DEPENDENT
        SOURCES ${ST_PLUGIN_SOURCES}
    )

    # CommonLib's CMake helper marks struct compatibility as dependent but its
    # generated RuntimeCompatibility initializer leaves SKSE's post-1.6.629
    # structure-layout bit unset. SKSE 2.3.1 rejects that combination with
    # "only compatible with versions earlier than 1.6.629". Set both the
    # Address Library and post-629 layout flags in the exported metadata.
    set(ST_PLUGIN_METADATA_FILE "${CMAKE_CURRENT_BINARY_DIR}/__${PROJECT_NAME}Plugin.cpp")
    file(READ "${ST_PLUGIN_METADATA_FILE}" ST_PLUGIN_METADATA)
    set(ST_UNPATCHED_RUNTIME_METADATA
        ".RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,")
    set(ST_PATCHED_RUNTIME_METADATA
        ".RuntimeCompatibility = { SKSE::VersionIndependence::AddressLibrary, true },")
    string(FIND "${ST_PLUGIN_METADATA}" "${ST_UNPATCHED_RUNTIME_METADATA}" ST_UNPATCHED_RUNTIME_METADATA_OFFSET)
    if(ST_UNPATCHED_RUNTIME_METADATA_OFFSET EQUAL -1)
        message(FATAL_ERROR "Unexpected CommonLib plugin metadata; cannot declare post-1.6.629 structure compatibility.")
    endif()
    string(REPLACE "${ST_UNPATCHED_RUNTIME_METADATA}" "${ST_PATCHED_RUNTIME_METADATA}"
        ST_PLUGIN_METADATA "${ST_PLUGIN_METADATA}")
    string(APPEND ST_PLUGIN_METADATA
        "\nconstexpr SKSE::PluginDeclaration::RuntimeCompatibility kSTRuntimeCompatibility{\n"
        "    SKSE::VersionIndependence::AddressLibrary, true\n"
        "};\nstatic_assert(kSTRuntimeCompatibility.Targets629Structs());\n")
    file(WRITE "${ST_PLUGIN_METADATA_FILE}" "${ST_PLUGIN_METADATA}")

    target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
    target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/SAL
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/SKSEMenuFramework)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ST_VERSION="${PROJECT_VERSION}")
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W3 /WX->
    )

    if(ST_DEPLOY_DIR)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${ST_DEPLOY_DIR}/SKSE/Plugins"
                "${ST_DEPLOY_DIR}/Interface/Translations"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${PROJECT_NAME}>
                "${ST_DEPLOY_DIR}/SKSE/Plugins/${PROJECT_NAME}.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json"
                "${ST_DEPLOY_DIR}/SKSE/Plugins/${PROJECT_NAME}.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/ST_TraitMenu.swf"
                "${ST_DEPLOY_DIR}/Interface/ST_TraitMenu.swf"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/Translations/SimpleTraits_ENGLISH.txt"
                "${ST_DEPLOY_DIR}/Interface/Translations/SimpleTraits_ENGLISH.txt"
            COMMENT "Deploying plugin files to ${ST_DEPLOY_DIR}"
            VERBATIM
        )
    endif()

    set(ST_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Deployed")
    add_custom_target(package_mod
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/package_mod.py"
            --dll $<TARGET_FILE:${PROJECT_NAME}>
            --config "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json"
            --swf "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/ST_TraitMenu.swf"
            --translation "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/Translations/SimpleTraits_ENGLISH.txt"
            --output-dir "${ST_PACKAGE_DIR}"
            --version "${PROJECT_VERSION}"
            --runtime "SE-AE"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating deterministic SimpleTraits mod package"
        VERBATIM
    )
endif()

if(BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    add_executable(SimpleTraitsRulesTests
        tests/TraitRulesTests.cpp
        src/TraitRules.cpp
    )
    target_compile_features(SimpleTraitsRulesTests PRIVATE cxx_std_23)
    target_include_directories(SimpleTraitsRulesTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME trait_rules COMMAND SimpleTraitsRulesTests)

    add_executable(SimpleTraitsSaveTests
        tests/TraitSaveTests.cpp
        src/TraitSave.cpp
        src/TraitRules.cpp
    )
    target_compile_features(SimpleTraitsSaveTests PRIVATE cxx_std_23)
    target_include_directories(SimpleTraitsSaveTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME trait_save_codec COMMAND SimpleTraitsSaveTests)

    add_executable(SimpleTraitsMenuRulesTests
        tests/TraitMenuRulesTests.cpp
        src/TraitMenuRules.cpp
    )
    target_compile_features(SimpleTraitsMenuRulesTests PRIVATE cxx_std_23)
    target_include_directories(SimpleTraitsMenuRulesTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME trait_menu_rules COMMAND SimpleTraitsMenuRulesTests)

    add_executable(SimpleTraitsHandshakeTests
        tests/HandshakeRulesTests.cpp
        src/HandshakeRules.cpp
    )
    target_compile_features(SimpleTraitsHandshakeTests PRIVATE cxx_std_23)
    target_include_directories(SimpleTraitsHandshakeTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/SAL)
    add_test(NAME sal_handshake COMMAND SimpleTraitsHandshakeTests)

    add_executable(SimpleTraitsLogPolicyTests
        tests/LogPolicyTests.cpp
        src/LogPolicy.cpp
    )
    target_compile_features(SimpleTraitsLogPolicyTests PRIVATE cxx_std_23)
    target_include_directories(SimpleTraitsLogPolicyTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME log_retention_policy COMMAND SimpleTraitsLogPolicyTests)

    add_test(NAME ui_asset_contract
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_ui_assets.py"
            --native "${CMAKE_CURRENT_SOURCE_DIR}/src/TraitMenu.cpp"
            --actionscript "${CMAKE_CURRENT_SOURCE_DIR}/assets/swf_src/scripts/frame_1/DoAction.as"
            --translation "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/Translations/SimpleTraits_ENGLISH.txt"
            --swf "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/ST_TraitMenu.swf")
    add_test(NAME release_defaults
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_release_defaults.py"
            --defaults "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json")
    if(ST_BUILD_PLUGIN)
        add_executable(SimpleTraitsSettingsTests
            tests/SettingsModelTests.cpp src/SettingsModel.cpp)
        target_compile_features(SimpleTraitsSettingsTests PRIVATE cxx_std_23)
        target_include_directories(SimpleTraitsSettingsTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(SimpleTraitsSettingsTests PRIVATE nlohmann_json::nlohmann_json)
        add_test(NAME settings_model COMMAND SimpleTraitsSettingsTests
            "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json")
    endif()
endif()
