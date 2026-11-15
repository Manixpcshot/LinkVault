cmake_minimum_required(VERSION 3.16)
project(LinkVault VERSION 3.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(LinkVault WIN32 MACOSX_BUNDLE
    main.cpp
    bookmark.h theme.h autostart.h backdrop.h
    fetcher.h fetcher.cpp
    titlebar.h titlebar.cpp
    card.h card.cpp
    widgets.h widgets.cpp
    dialogs.h dialogs.cpp
    settingspage.h settingspage.cpp
    mainwindow.h mainwindow.cpp mainwindow_v3.cpp
    hotkey.h hotkey.cpp
    popups.h popups.cpp
    linkchecker.h linkchecker.cpp
    sync.h sync.cpp
)
if(WIN32)
    # exe icon. Built as a separate object lib WITHOUT Qt include dirs:
    # MinGW windres breaks on include paths containing spaces.
    add_library(LinkVaultIcon OBJECT app.rc)
    target_sources(LinkVault PRIVATE $<TARGET_OBJECTS:LinkVaultIcon>)
endif()

qt_add_resources(LinkVault "assets" PREFIX "/" FILES icon.png)
target_link_libraries(LinkVault PRIVATE Qt6::Widgets Qt6::Network)
if(WIN32)
    target_link_libraries(LinkVault PRIVATE dwmapi)   # rounded corners on Windows 11
endif()
