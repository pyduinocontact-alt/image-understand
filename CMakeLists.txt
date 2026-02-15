cmake_minimum_required(VERSION 3.5)

project(image-understand VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt5
find_package(Qt5 COMPONENTS Core Widgets REQUIRED)

# Enable automoc for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Source files
set(SOURCES
    main.cpp
    imageunderstandgui.cpp
)

set(HEADERS
    imageunderstandgui.h
)

# Create executable
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
)

# Link Qt libraries
target_link_libraries(${PROJECT_NAME}
    Qt5::Core
    Qt5::Widgets
)

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy Python backend to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/image_understand_backend.py
    ${CMAKE_BINARY_DIR}/bin/image_understand_backend.py
)

# Installation rules
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(FILES image_understand_backend.py
    DESTINATION bin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Windows specific
if(WIN32)
    # Copy Qt DLLs
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(QT_INSTALL_PATH "${Qt5_DIR}/../../../bin")
    else()
        set(QT_INSTALL_PATH "${Qt5_DIR}/../../../bin")
    endif()
endif()

# Print build information
message(STATUS "Building ${PROJECT_NAME} version ${PROJECT_VERSION}")
message(STATUS "Qt5 version: ${Qt5_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")