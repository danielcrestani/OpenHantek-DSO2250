get_target_property(QT_BIN_DIR Qt${QT_VERSION_MAJOR}::Core LOCATION)
get_filename_component(QT_BIN_DIR "${QT_BIN_DIR}" DIRECTORY)
if (NOT WIN32 OR NOT EXISTS "${QT_BIN_DIR}/windeployqt.exe")
    return()
endif()

# install Qt Dlls into the binary dir on windows platforms
if (MSVC)
    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${QT_BIN_DIR}/qtenv2.bat"
        COMMAND "${QT_BIN_DIR}/windeployqt" --no-translations "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIGURATION>/${PROJECT_NAME}.exe"
        WORKING_DIRECTORY "${QT_BIN_DIR}"
        COMMENT "Copy Qt dlls for ${PROJECT_NAME}"
    )
else()
    if ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
        set(ADD_OPT "--release")
    endif()
    add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${QT_BIN_DIR}/qtenv2.bat"
        COMMAND "${QT_BIN_DIR}/windeployqt" --no-translations ${ADD_OPT} "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.exe"
        WORKING_DIRECTORY "${QT_BIN_DIR}"
        COMMENT "Copy Qt dlls for ${PROJECT_NAME}"
    )
endif()
