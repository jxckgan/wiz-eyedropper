cmake_minimum_required(VERSION 3.10)
project(WizLedController VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Windows-specific configurations
if(WIN32)
    enable_language(RC)
    set(WIN_RC_FILE ${SRC_DIR}/app.rc)
    set(APP_ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/meta/app.ico)
    
    if(NOT EXISTS ${CMAKE_BINARY_DIR}/app.ico)
        configure_file(${APP_ICON_FILE} ${CMAKE_BINARY_DIR}/app.ico COPYONLY)
    endif()
    
    if(NOT EXISTS ${CMAKE_BINARY_DIR}/resource.h)
        configure_file(${SRC_DIR}/resource.h ${CMAKE_BINARY_DIR}/resource.h COPYONLY)
    endif()
    
    # Add Windows app manifest
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
    set(APP_MANIFEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/meta/app.manifest)
    
    # Define Windows executable type (GUI application)
    set(CMAKE_WIN32_EXECUTABLE ON)
endif()

# Find Qt paths based on platform
if(WIN32)
    set(CMAKE_PREFIX_PATH 
        "C:/Qt/5.15.2/msvc2019_64"
        "C:/Qt/5.15.2/msvc2019"
        "${CMAKE_CURRENT_SOURCE_DIR}/qt-5.15.2/msvc2019_64"
    )
else()
    set(CMAKE_PREFIX_PATH 
        "/usr/local/opt/qt5"
        "/opt/homebrew/opt/qt@5"
        "$ENV{HOME}/Qt/5.15.2/clang_64"
    )
endif()

# Find required Qt packages
find_package(Qt5 COMPONENTS Core Widgets Network REQUIRED)

# Source files
set(SOURCES
    src/main.cpp
)

# Add Windows resources if on Windows
if(WIN32)
    set(SOURCES ${SOURCES} ${WIN_RC_FILE})
    
    # Add app manifest file if it exists
    if(EXISTS ${APP_MANIFEST_FILE})
        set(SOURCES ${SOURCES} ${APP_MANIFEST_FILE})
    endif()
endif()

# Create executable
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES})
endif()

# Link Qt libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Widgets
    Qt5::Network
)

# Add platform-specific link dependencies
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dwmapi  # Windows DWM API for modern window styling
        user32  # User interface functions
    )
endif()

# Install targets
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Copy Qt DLLs to output folder on Windows
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying Qt DLLs to output directory..."
        COMMAND ${Qt5_DIR}/../../../bin/windeployqt.exe --no-translations --no-system-d3d-compiler --no-opengl-sw $<TARGET_FILE:${PROJECT_NAME}>
    )
endif()