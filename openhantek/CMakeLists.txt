project(OpenHantek CXX)

# Qt 6 is the default; Qt 5.15 still works (cmake -DOPENHANTEK_QT5=ON).
option(OPENHANTEK_QT5 "Build with Qt 5 instead of Qt 6" OFF)
if (OPENHANTEK_QT5)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets PrintSupport OpenGL)
    set(QT_VERSION_MAJOR 5)
    set(OPENHANTEK_QT_LIBS Qt5::Widgets Qt5::PrintSupport Qt5::OpenGL)
else()
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets PrintSupport OpenGL OpenGLWidgets)
    set(QT_VERSION_MAJOR 6)
    set(OPENHANTEK_QT_LIBS Qt6::Widgets Qt6::PrintSupport Qt6::OpenGL Qt6::OpenGLWidgets)
endif()
message(STATUS "Building with Qt ${QT_VERSION_MAJOR}")
find_package(OpenGL)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# include directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(src/ src/hantekdso src/widgets src/docks src/configdialog)

# collect sources and other files
file(GLOB_RECURSE SRC "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")
file(GLOB_RECURSE UI "src/*.ui")
file(GLOB_RECURSE QRC "res/*.qrc")

add_custom_target(format SOURCES ".clang-format"
    COMMAND "clang-format" "-style=file" "-i" "-sort-includes" ${SRC} ${HEADERS})

add_subdirectory(translations)

add_definitions(-DVERSION="${CPACK_PACKAGE_VERSION}")

set(EXECTYPE "")
if(WIN32 AND MINGW)
    set(EXECTYPE WIN32)
endif()

# make executable
add_executable(${PROJECT_NAME} ${EXECTYPE} ${SRC} ${HEADERS} ${UI} ${QRC} ${TRANSLATION_BIN_FILES} ${TRANSLATION_QRC})
target_link_libraries(${PROJECT_NAME} ${OPENHANTEK_QT_LIBS} ${OPENGL_LIBRARIES} )
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_range_for)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/W4" "/wd4251" "/wd4127" "/wd4275" "/wd4200" "/nologo" "/J" "/Zi")
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:/MDd>")
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic)
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:-DDEBUG>")
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:-O0>")
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:RELEASE>:-fno-rtti>")
endif()

include(../cmake/fftw_on_windows.cmake)
include(../cmake/libusb_on_windows.cmake)

if(NOT WIN32)
    find_package(libusb REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} ${LIBUSB_LIBRARIES})

    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})

    find_package(FFTW REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFTW_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} ${FFTW_LIBRARIES})
endif()

# install commands
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin")

include(../cmake/copy_qt_dlls_to_bin_dir.cmake)
