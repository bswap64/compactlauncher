cmake_minimum_required(VERSION 3.16)
project(CompactLauncher VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Core Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Widgets REQUIRED)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

set(SOURCES
    main.cpp
    utils.cpp
    download_manager.cpp
    downloader_dialog.cpp
    settings_dialog.cpp
    main_frame.cpp
    resources.qrc
)

if(WIN32)
    list(APPEND SOURCES app.rc)
    add_executable(CompactLauncher WIN32 ${SOURCES})
    set_target_properties(CompactLauncher PROPERTIES OUTPUT_NAME "CompactLauncher")
    if(QT_VERSION_MAJOR EQUAL 6)
        target_link_libraries(CompactLauncher PRIVATE Qt6::Core Qt6::Widgets)
    else()
        target_link_libraries(CompactLauncher PRIVATE Qt5::Core Qt5::Widgets)
    endif()
    target_link_libraries(CompactLauncher PRIVATE wininet ws2_32 advapi32 ole32 oleaut32 shell32 z)
else()
    add_executable(CompactLauncher ${SOURCES})
    set_target_properties(CompactLauncher PROPERTIES OUTPUT_NAME "CompactLauncher")
    if(QT_VERSION_MAJOR EQUAL 6)
        target_link_libraries(CompactLauncher PRIVATE Qt6::Core Qt6::Widgets)
    else()
        target_link_libraries(CompactLauncher PRIVATE Qt5::Core Qt5::Widgets)
    endif()
    find_package(CURL REQUIRED)
    target_link_libraries(CompactLauncher PRIVATE CURL::libcurl)
endif()

if(MSVC)
    target_compile_options(CompactLauncher PRIVATE /W3 /O2 /MP)
    set_property(TARGET CompactLauncher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(CompactLauncher PRIVATE -Wall -O2)
endif()

target_compile_definitions(CompactLauncher PRIVATE
    UNICODE _UNICODE
    _CRT_SECURE_NO_WARNINGS
)
