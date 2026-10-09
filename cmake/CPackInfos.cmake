# This file configures CPack. We setup a version number that contains
# the current git revision if git is found. A zip file is created on
# all platforms. Additionally an NSIS Installer exe is created on windows
# and a .sh installer file for linux.

find_package(Git QUIET)

if (GIT_EXECUTABLE AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE CMD_RESULT
        OUTPUT_VARIABLE VCS_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        )
endif()

if(NOT DEFINED CMD_RESULT)
    set(VCS_BRANCH "master")
    set(VCS_REVISION "na")
else()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE CMD_RESULT
        OUTPUT_VARIABLE DESCRIBE_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )

    string(REPLACE "\n" " " DESCRIBE_STATUS ${DESCRIBE_STATUS})
    string(REPLACE "\r" " " DESCRIBE_STATUS ${DESCRIBE_STATUS})
    string(REPLACE "\rn" " " DESCRIBE_STATUS ${DESCRIBE_STATUS})
    string(REPLACE " " ";" DESCRIBE_STATUS ${DESCRIBE_STATUS})
    list(GET DESCRIBE_STATUS 2 VCS_BRANCH)

    message(STATUS "Version: ${VCS_BRANCH}/${VCS_REVISION}")

    execute_process(
        COMMAND ${GIT_EXECUTABLE} config --get remote.origin.url
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE CMD_RESULT
        OUTPUT_VARIABLE VCS_URL
        OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        
    set(ENV{LANG} "en_US")
    if(GIT_VERSION_STRING VERSION_LESS 2.6)
        set(CHANGELOG "")
    else()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} log -n 10 "--date=format:%a %b %d %Y" "--pretty=format:* %ad %aN <%aE> %h - %s"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE CMD_RESULT
            OUTPUT_VARIABLE CHANGELOG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            )
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/changelog" "${CHANGELOG}")
endif()

string(TIMESTAMP DATE_VERSION "%d.%m.%Y")
set(VCS_COUNT 0)
if (GIT_EXECUTABLE AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE VCS_COUNT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if (NOT VCS_COUNT)
        set(VCS_COUNT 0)
    endif()
endif()
string(TIMESTAMP CURRENT_TIME "%d.%m.%Y %H:%M")

if (UNIX)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_GENERATOR STGZ)
	if (NOT APPLE)
        set(CPACK_GENERATOR ${CPACK_GENERATOR} ZIP)
        find_program(LSB_RELEASE lsb_release)
        execute_process(COMMAND ${LSB_RELEASE} -is
            OUTPUT_VARIABLE LSB_RELEASE_ID_SHORT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (LSB_RELEASE_ID_SHORT MATCHES "Ubuntu")
            set(CPACK_GENERATOR ${CPACK_GENERATOR} DEB)
        else()
            set(CPACK_GENERATOR ${CPACK_GENERATOR} RPM)
        endif()
	endif()
elseif(WIN32)
    set(CPACK_GENERATOR NSIS)
endif()

set(CPACK_PACKAGE_NAME "openhantek-dso2250")
# Debian versions must start with a digit and grow with every build: 3.4.<commits>+g<revision>
set(CPACK_PACKAGE_VERSION "3.4.${VCS_COUNT}+g${VCS_REVISION}")
set(CPACK_PACKAGE_CONTACT "Daniel Crestani <https://github.com/danielcrestani/OpenHantek-DSO2250>")
set(CPACK_PACKAGE_VENDOR "OpenHantek Community")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OpenHantek (Hantek DSO-2250), PSG9080 generator and Bode analyzer")
set(CPACK_PACKAGE_DESCRIPTION "Three programs with a dark instrument look:\n OpenHantek - oscilloscope and FFT for Hantek USB oscilloscopes (DSO-2250 tested);\n PSG9080 - control of the Joy-IT / JunTek PSG9080 function generator;\n OpenHantekBode - frequency response (Bode plot) with both instruments.")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/readme.md")
if (EXISTS "${CMAKE_SOURCE_DIR}/COPYING")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
endif()

# Linux DEB+RPM 
set(CPACK_DEBIAN_PACKAGE_SECTION "electronics")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/danielcrestani/OpenHantek-DSO2250")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT) # openhantek-dso2250_<version>_amd64.deb
# shared libraries found by dpkg-shlibdeps; the Qt platform plugins (X11/Wayland window) are loaded at run time
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-translations-l10n, qt6-wayland")
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "openhantek")
set(CPACK_DEBIAN_PACKAGE_REPLACES "openhantek")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/packaging/linux/postinst;${CMAKE_SOURCE_DIR}/packaging/linux/postrm")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
set(CPACK_ARCH)
IF ((MSVC AND CMAKE_GENERATOR MATCHES "Win64+") OR (CMAKE_SIZEOF_VOID_P EQUAL 8))
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
    set(CPACK_ARCH "x86_64")
else()
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i586")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "i586")
    set(CPACK_ARCH "x86")
endif()
set(CPACK_STRIP_FILES 1)

include(CMakeDetermineSystem)

# Linux RPM
set(CPACK_RPM_PACKAGE_RELOCATABLE NO)
set(CPACK_RPM_PACKAGE_LICENSE "GPLv2+")
set(CPACK_RPM_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION})
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase-gui%{?_isa} >= 6.2, qt6-qttranslations%{?_isa}")
set(CPACK_RPM_CHANGELOG_FILE "${CMAKE_BINARY_DIR}/changelog")

set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-1.${CPACK_ARCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ".")
SET(CPACK_OUTPUT_FILE_PREFIX packages)

include(CPack)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
include(InstallRequiredSystemLibraries)

cpack_add_install_type(Full DISPLAY_NAME "All")

set(VERSION ${CPACK_PACKAGE_VERSION})
