cmake_minimum_required(VERSION 3.16)

project(gamex
    VERSION 0.1.0
    DESCRIPTION "Linux memory editor for local single-player games"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GAMEX_BUILD_UI "Build the Qt interface" ON)
option(GAMEX_BUILD_TESTS "Build core tests" ON)

# Відображувана версія: major.minor.<номер_збірки>. Номер ++ при кожній збірці
# gamex (файл cmake/build_number, див. write_build_info.cmake).
set(GAMEX_VERSION_STRING
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.0")
string(TIMESTAMP GAMEX_BUILD_TIME "%Y-%m-%dT%H:%M:%S" UTC)
string(TIMESTAMP GAMEX_BUILD_TIME_READABLE "%d.%m.%Y %H:%M:%S UTC" UTC)
set(GAMEX_BUILD_NUMBER "0")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gamex_build_info.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/gamex_build_info.hpp"
    @ONLY)

add_library(gamex_core STATIC
    src/core/memory_region.cpp
    src/core/process_memory.cpp
    src/core/value_codec.cpp
    src/core/expression.cpp
    src/core/byte_pattern.cpp
    src/core/search_value.cpp
    src/core/scanner.cpp
    src/core/module_map.cpp
    src/core/pointer_scan.cpp
    src/core/dump_book.cpp
    src/core/structure.cpp
    src/core/watchpoint.cpp
    src/core/emulator_profile.cpp
    src/core/address_table.cpp
    src/core/freezer.cpp
)
target_include_directories(gamex_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(gamex_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CAPSTONE QUIET capstone)
    pkg_check_modules(X11 QUIET x11)
endif()
if(CAPSTONE_FOUND)
    target_compile_definitions(gamex_core PUBLIC GAMEX_HAS_CAPSTONE=1)
    target_include_directories(gamex_core PUBLIC ${CAPSTONE_INCLUDE_DIRS})
    target_link_libraries(gamex_core PUBLIC ${CAPSTONE_LIBRARIES})
    message(STATUS "Capstone found — watchpoint disassembly enabled")
else()
    message(STATUS "Capstone not found — watchpoint shows instruction bytes only")
endif()
if(X11_FOUND)
    target_compile_definitions(gamex_core PUBLIC GAMEX_HAS_X11=1)
    target_include_directories(gamex_core PUBLIC ${X11_INCLUDE_DIRS})
    target_link_libraries(gamex_core PUBLIC ${X11_LIBRARIES})
    message(STATUS "X11 found — shell panel process filter enabled")
else()
    message(STATUS "X11 not found — shell panel process filter disabled")
endif()

if(GAMEX_BUILD_TESTS)
    enable_testing()
    add_executable(gamex_core_tests tests/test_core.cpp)
    target_link_libraries(gamex_core_tests PRIVATE gamex_core)
    target_compile_options(gamex_core_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    add_test(NAME core COMMAND gamex_core_tests)
endif()

if(GAMEX_BUILD_UI)
    # Порожньо = системний Qt. На іншій машині:
    #   cmake -DGAMEX_QT_ROOT=$HOME/Qt/6.12.0/gcc_64 ...
    # або: export CMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/gcc_64
    set(GAMEX_QT_ROOT "" CACHE PATH "Optional Qt 6/5 kit prefix (…/gcc_64)")
    if(GAMEX_QT_ROOT)
        if(EXISTS "${GAMEX_QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake"
           OR EXISTS "${GAMEX_QT_ROOT}/lib/cmake/Qt5/Qt5Config.cmake")
            list(PREPEND CMAKE_PREFIX_PATH "${GAMEX_QT_ROOT}")
        else()
            message(WARNING "GAMEX_QT_ROOT=${GAMEX_QT_ROOT} не містить Qt Config.cmake — ігнорую")
        endif()
    endif()
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        set(GAMEX_QT_LIB Qt6::Widgets)
    else()
        find_package(Qt5 COMPONENTS Widgets QUIET)
        if(Qt5_FOUND)
            set(GAMEX_QT_LIB Qt5::Widgets)
        endif()
    endif()

    if(GAMEX_QT_LIB)
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        add_executable(gamex
            src/main.cpp
            src/ui/main_window.hpp
            src/ui/main_window.cpp
            src/ui/process_dialog.hpp
            src/ui/process_dialog.cpp
            src/ui/region_dialog.hpp
            src/ui/region_dialog.cpp
            src/ui/pointer_dialog.hpp
            src/ui/pointer_dialog.cpp
            src/ui/hex_dialog.hpp
            src/ui/hex_dialog.cpp
            src/ui/structure_dialog.hpp
            src/ui/structure_dialog.cpp
            src/ui/watch_dialog.hpp
            src/ui/watch_dialog.cpp
            src/ui/help_dialog.hpp
            src/ui/help_dialog.cpp
            src/ui/scan_worker.hpp
            src/ui/scan_worker.cpp
            resources/gamex.qrc
        )
        target_link_libraries(gamex PRIVATE gamex_core ${GAMEX_QT_LIB})
        target_include_directories(gamex PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_compile_options(gamex PRIVATE -Wall -Wextra -Wpedantic)
        set(GAMEX_BUILD_INFO_HEADER
            "${CMAKE_CURRENT_BINARY_DIR}/generated/gamex_build_info.hpp")
        add_custom_target(gamex_build_info
            COMMAND ${CMAKE_COMMAND}
                -DGAMEX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                -DGAMEX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
                -DGAMEX_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DGAMEX_COUNTER_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_number
                -DGAMEX_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/gamex_build_info.hpp.in
                -DGAMEX_OUTPUT=${GAMEX_BUILD_INFO_HEADER}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_build_info.cmake
            BYPRODUCTS ${GAMEX_BUILD_INFO_HEADER}
            COMMENT "Bumping GameX build number / version"
            VERBATIM)
        add_dependencies(gamex gamex_build_info)
        set_source_files_properties(
            src/main.cpp
            src/ui/main_window.cpp
            PROPERTIES OBJECT_DEPENDS "${GAMEX_BUILD_INFO_HEADER}")

        # Qt з ~/Qt/… (не з /usr): прописуємо RPATH на його lib/,
        # інакше після install підвантажиться системний Qt і буде
        # «version Qt_6.xx not found».
        if(GAMEX_QT_ROOT AND EXISTS "${GAMEX_QT_ROOT}/lib")
            set_target_properties(gamex PROPERTIES
                BUILD_RPATH "${GAMEX_QT_ROOT}/lib"
                INSTALL_RPATH "${GAMEX_QT_ROOT}/lib"
                INSTALL_RPATH_USE_LINK_PATH TRUE)
        endif()

        include(GNUInstallDirs)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/data/gamex.desktop.in"
            "${CMAKE_CURRENT_BINARY_DIR}/gamex.desktop"
            @ONLY)

        install(TARGETS gamex
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/gamex.desktop"
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/gamex.svg"
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
            RENAME gamex.svg)
        foreach(size 48 64 128 256)
            install(FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/hicolor/${size}x${size}/apps/gamex.png"
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps
                OPTIONAL)
        endforeach()
        install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/CONTRIBUTING.md"
            DESTINATION ${CMAKE_INSTALL_DOCDIR})
    else()
        message(WARNING
            "Qt Widgets dev files were not found. Built gamex_core only. "
            "Install qt6-base-dev (or qtbase5-dev) and reconfigure.")
    endif()
endif()
