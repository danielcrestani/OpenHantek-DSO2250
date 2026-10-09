cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(OpenHantekProject)

set(OpenGL_GL_PREFERENCE GLVND)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
IF(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
   SET(CMAKE_BUILD_TYPE RelWithDebInfo)
ENDIF(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)

# Find external libraries
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/")

INCLUDE_DIRECTORIES(".")

# Use CPack to make deb/rpm/zip/exe installer packages
include(cmake/CPackInfos.cmake)

# Qt Widgets based Gui with OpenGL canvas
add_subdirectory(openhantek)

if (WIN32)
    install(FILES COPYING readme.md DESTINATION ".")
else()
    add_subdirectory(firmware EXCLUDE_FROM_ALL)
endif()

if("${CMAKE_SYSTEM}" MATCHES "Linux")
  # udev rule: the logged-in user gets access to the oscilloscope without sudo
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/firmware/60-hantek.rules" DESTINATION "/lib/udev/rules.d/")
  # application menu entries and icons of the three programs
  foreach(app openhantek psg9080 openhantek-bode)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/${app}.desktop" DESTINATION "share/applications")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/openhantek/res/icons/${app}.svg"
      DESTINATION "share/icons/hicolor/scalable/apps")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/openhantek/res/icons/${app}.png"
      DESTINATION "share/icons/hicolor/128x128/apps")
  endforeach()
  install(FILES COPYING DESTINATION "share/doc/openhantek-dso2250" RENAME copyright)
endif()

# Add auxiliary files to the project, so that these files appear in VisualStudio/QtCreator
file(GLOB_RECURSE MDFILES "docs/*.md" "openhantek/*.md")
add_custom_target(readme SOURCES readme.md ${MDFILES})

# Add "cppcheck" command
add_custom_target(cppcheck COMMAND "cppcheck --enable=all -I \"${CMAKE_CURRENT_LIST_DIR}/openhantek/src\" -q ${SRC} --template=\"{file}:{line}: {severity}: {message}\"")

# Add "doc" target to build the documentation.
find_package(Doxygen QUIET)
if (DOXYGEN_FOUND)
    add_custom_target(doc
        COMMAND "${CMAKE_COMMAND} -E remove_directory html"
        COMMAND "${DOXYGEN_EXECUTABLE} Doxyfile" WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
endif()
