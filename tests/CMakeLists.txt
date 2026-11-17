find_package(Qt6 6.4 REQUIRED COMPONENTS Test)

# Both live D-Bus tests reach a private daemon over TCP; see support/addrconfig_shim.c. The target is
# created unconditionally because it is independent of the optional AT-SPI backend.
add_library(addrconfig_shim SHARED support/addrconfig_shim.c)
target_link_libraries(addrconfig_shim PRIVATE ${CMAKE_DL_LIBS})

# One executable per test file keeps failures attributable and lets `ctest -R core` select a module.
function(sniplyngo_add_test name)
    # The fake headers contain Q_OBJECT classes; listing them lets AUTOMOC generate their metaobjects.
    add_executable(${name}
        ${name}.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/fakes/fake_provider.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/fakes/fake_selection_backend.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/fakes/stub_http_server.h"
    )
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/fakes")
    target_link_libraries(${name} PRIVATE
        sniplyngo_core sniplyngo_settings sniplyngo_selection sniplyngo_provider
        sniplyngo_coordinator sniplyngo_secrets sniplyngo_app sniplyngo_ui
        Qt6::Test Qt6::Network Qt6::DBus Qt6::Gui)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

sniplyngo_add_test(core_error_policy_test)
sniplyngo_add_test(core_generation_test)
sniplyngo_add_test(core_delay_test)
sniplyngo_add_test(core_paths_test)
sniplyngo_add_test(settings_model_test)
sniplyngo_add_test(settings_store_test)
sniplyngo_add_test(selection_selector_test)
sniplyngo_add_test(provider_sse_parser_test)
sniplyngo_add_test(provider_openai_test)
sniplyngo_add_test(coordinator_test)
sniplyngo_add_test(secrets_test)
sniplyngo_add_test(app_cli_test)
sniplyngo_add_test(app_capability_report_test)
sniplyngo_add_test(selection_reading_test)
sniplyngo_add_test(app_clipboard_test)
set_tests_properties(app_clipboard_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
sniplyngo_add_test(x11_protocol_test)
if(TARGET sniplyngo_platform_x11)
    sniplyngo_add_test(x11_backend_live_test)
    target_link_libraries(x11_backend_live_test PRIVATE sniplyngo_platform_x11)
endif()
sniplyngo_add_test(provider_simple_translate_test)
sniplyngo_add_test(app_provider_factory_test)
target_link_libraries(app_provider_factory_test PRIVATE sniplyngo_runtime)
sniplyngo_add_test(app_runtime_integration_test)
target_compile_definitions(app_runtime_integration_test PRIVATE
    SNIPLYNGO_EXECUTABLE="$<TARGET_FILE:sniplyngo>")
add_dependencies(app_runtime_integration_test sniplyngo)
sniplyngo_add_test(secrets_service_live_test)
target_sources(secrets_service_live_test PRIVATE
    fakes/fake_secret_service.cpp
    fakes/fake_secret_service.h)
set_tests_properties(secrets_service_live_test PROPERTIES
    ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:addrconfig_shim>")
sniplyngo_add_test(ui_result_controller_test)
set_tests_properties(ui_result_controller_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

sniplyngo_add_test(settings_controller_test)
set_tests_properties(settings_controller_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

sniplyngo_add_test(ui_settings_qml_test)
target_compile_definitions(ui_settings_qml_test PRIVATE
    SNIPLYNGO_QML_DIR="${CMAKE_SOURCE_DIR}/src/ui/qml")
target_link_libraries(ui_settings_qml_test PRIVATE Qt6::Qml Qt6::Quick)
set_tests_properties(ui_settings_qml_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

sniplyngo_add_test(ui_qml_load_test)
target_compile_definitions(ui_qml_load_test PRIVATE
    SNIPLYNGO_QML_DIR="${CMAKE_SOURCE_DIR}/src/ui/qml")
target_link_libraries(ui_qml_load_test PRIVATE Qt6::Qml Qt6::Quick)
set_tests_properties(ui_qml_load_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Only meaningful when the AT-SPI backend was built; it needs a private D-Bus daemon on TCP.
if(TARGET sniplyngo_platform_atspi)
    sniplyngo_add_test(atspi_backend_live_test)
    target_include_directories(atspi_backend_live_test PRIVATE ${SNIPLYNGO_ATSPI_INCLUDE_DIR})
    set_tests_properties(atspi_backend_live_test PROPERTIES
        ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:addrconfig_shim>;QT_NO_GLIB=1")
    target_sources(atspi_backend_live_test PRIVATE
        fakes/fake_atspi_service.cpp
        fakes/fake_atspi_service.h)
    target_link_libraries(atspi_backend_live_test PRIVATE sniplyngo_platform_atspi)
endif()
