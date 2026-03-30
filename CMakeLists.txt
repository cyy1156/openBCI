cmake_minimum_required(VERSION 3.19)
project(QtBCI LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(QtBCI
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    thinkgear/thinkgear.h
)

target_link_libraries(QtBCI
    PRIVATE
        Qt::Core
        Qt::Widgets
)

#
# ThinkGear Stream SDK for PC（仅 x64）
#
# 说明：
# - 你现在把 `thinkgear.h` 放到了 `QtBCI/QtBCI/thinkgear/thinkgear.h`
# - 这里同时需要 x64 的 `thinkgear64.lib` + `thinkgear64.dll` 用于链接与运行时拷贝
#
set(THINKGEAR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thinkgear")

set(THINKGEAR_INC_DIR "${THINKGEAR_ROOT}")

if (NOT EXISTS "${THINKGEAR_INC_DIR}/thinkgear.h")
    message(FATAL_ERROR "thinkgear.h not found. Expect: ${THINKGEAR_INC_DIR}/thinkgear.h")
endif()

# 允许你在不同目录结构下放置 lib/dll（只要存在其中一种就行）
set(_THINKGEAR_LIB_CANDIDATES
    "${THINKGEAR_ROOT}/libs/x64/thinkgear64.lib"
    "${THINKGEAR_ROOT}/x64/thinkgear64.lib"
    "${THINKGEAR_ROOT}/thinkgear64.lib"
)

set(_THINKGEAR_DLL_CANDIDATES
    "${THINKGEAR_ROOT}/libs/x64/thinkgear64.dll"
    "${THINKGEAR_ROOT}/x64/thinkgear64.dll"
    "${THINKGEAR_ROOT}/thinkgear64.dll"
)

set(THINKGEAR_LIB "")
foreach(p IN LISTS _THINKGEAR_LIB_CANDIDATES)
    if (EXISTS "${p}")
        set(THINKGEAR_LIB "${p}")
        break()
    endif()
endforeach()

set(THINKGEAR_DLL "")
foreach(p IN LISTS _THINKGEAR_DLL_CANDIDATES)
    if (EXISTS "${p}")
        set(THINKGEAR_DLL "${p}")
        break()
    endif()
endforeach()

if (THINKGEAR_LIB STREQUAL "")
    message(FATAL_ERROR "thinkgear64.lib not found. Put it under one of: ${_THINKGEAR_LIB_CANDIDATES}")
endif()

if (THINKGEAR_DLL STREQUAL "")
    message(FATAL_ERROR "thinkgear64.dll not found. Put it under one of: ${_THINKGEAR_DLL_CANDIDATES}")
endif()

target_include_directories(QtBCI
    PRIVATE
        "${THINKGEAR_INC_DIR}"
)

target_link_libraries(QtBCI
    PRIVATE
        "${THINKGEAR_LIB}"
)

add_custom_command(TARGET QtBCI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${THINKGEAR_DLL}"
        "$<TARGET_FILE_DIR:QtBCI>"
)

include(GNUInstallDirs)

install(TARGETS QtBCI
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET QtBCI
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
