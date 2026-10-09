project(OpenHantek CXX)

# Qt 6 is the default; Qt 5.15 still works (cmake -DOPENHANTEK_QT5=ON).
option(OPENHANTEK_QT5 "Build with Qt 5 instead of Qt 6" OFF)
if (OPENHANTEK_QT5)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets PrintSupport OpenGL SerialPort)
    set(QT_VERSION_MAJOR 5)
    set(OPENHANTEK_QT_LIBS Qt5::Widgets Qt5::PrintSupport Qt5::OpenGL Qt5::SerialPort)
else()
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets PrintSupport OpenGL OpenGLWidgets SerialPort)
    set(QT_VERSION_MAJOR 6)
    set(OPENHANTEK_QT_LIBS Qt6::Widgets Qt6::PrintSupport Qt6::OpenGL Qt6::OpenGLWidgets Qt6::SerialPort)
endif()
message(STATUS "Building with Qt ${QT_VERSION_MAJOR}")
find_package(OpenGL)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# include directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(src/ src/hantekdso src/widgets src/docks src/configdialog)

# ------------------------------------------------------------------------------------------------ sources
# Shared parts (object libraries, so the self-registering DSO models and the resources are always linked):
#   openhantek_core - USB, DSO-2250 driver, protocol, device selection, firmwares
#   psg9080_core    - PSG9080 protocol, serial connection and front panel
# Programs:
#   OpenHantek      - oscilloscope and FFT
#   PSG9080         - function generator control
#   OpenHantekBode  - frequency response (PSG9080 + DSO-2250)
file(GLOB_RECURSE CORE_SRC src/usb/*.cpp src/hantekdso/*.cpp src/hantekprotocol/*.cpp src/utils/*.cpp
     src/selectdevice/*.cpp)
file(GLOB_RECURSE CORE_HDR src/usb/*.h src/hantekdso/*.h src/hantekprotocol/*.h src/utils/*.h src/selectdevice/*.h)
file(GLOB_RECURSE CORE_UI src/selectdevice/*.ui)
file(GLOB GEN_SRC src/generator/*.cpp)
file(GLOB GEN_HDR src/generator/*.h)
file(GLOB BODE_SRC src/bode/*.cpp)
file(GLOB BODE_HDR src/bode/*.h)

file(GLOB_RECURSE SRC "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")
file(GLOB_RECURSE UI "src/*.ui")
set(SHARED_DIRS "/src/(usb|hantekdso|hantekprotocol|utils|selectdevice|generator|bode|apps)/")
list(FILTER SRC EXCLUDE REGEX "${SHARED_DIRS}")
list(FILTER HEADERS EXCLUDE REGEX "${SHARED_DIRS}")
list(FILTER UI EXCLUDE REGEX "${SHARED_DIRS}")

add_custom_target(format SOURCES ".clang-format"
    COMMAND "clang-format" "-style=file" "-i" "-sort-includes" ${SRC} ${HEADERS} ${CORE_SRC} ${CORE_HDR}
            ${GEN_SRC} ${GEN_HDR} ${BODE_SRC} ${BODE_HDR})

add_subdirectory(translations)

add_definitions(-DVERSION="${CPACK_PACKAGE_VERSION}")

set(EXECTYPE "")
if(WIN32 AND MINGW)
    set(EXECTYPE WIN32)
endif()

if(NOT WIN32)
    find_package(libusb REQUIRED)
    find_package(Threads REQUIRED)
    find_package(FFTW REQUIRED)
endif()
include(../cmake/fftw_on_windows.cmake)

# Same language level and warnings everywhere
function(openhantek_setup TARGET)
    set_target_properties(${TARGET} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE "/W4" "/wd4251" "/wd4127" "/wd4275" "/wd4200" "/nologo" "/J" "/Zi")
        target_compile_options(${TARGET} PRIVATE "$<$<CONFIG:DEBUG>:/MDd>")
    else()
        target_compile_options(${TARGET} PRIVATE -Wall -Wno-long-long -pedantic)
        target_compile_options(${TARGET} PRIVATE "$<$<CONFIG:DEBUG>:-DDEBUG>")
        target_compile_options(${TARGET} PRIVATE "$<$<CONFIG:DEBUG>:-O0>")
        target_compile_options(${TARGET} PRIVATE "$<$<CONFIG:RELEASE>:-fno-rtti>")
    endif()
endfunction()

# --- openhantek_core
add_library(openhantek_core OBJECT ${CORE_SRC} ${CORE_HDR} ${CORE_UI} res/application.qrc res/firmwares.qrc)
target_link_libraries(openhantek_core PUBLIC ${OPENHANTEK_QT_LIBS})
openhantek_setup(openhantek_core)
if(NOT WIN32)
    target_include_directories(openhantek_core PUBLIC ${LIBUSB_INCLUDE_DIRS})
    target_link_libraries(openhantek_core PUBLIC ${LIBUSB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif()

# --- psg9080_core
add_library(psg9080_core OBJECT ${GEN_SRC} ${GEN_HDR})
target_link_libraries(psg9080_core PUBLIC ${OPENHANTEK_QT_LIBS})
openhantek_setup(psg9080_core)

# --- OpenHantek: oscilloscope and FFT
add_executable(${PROJECT_NAME} ${EXECTYPE} ${SRC} ${HEADERS} ${UI} res/configdialog.qrc ${TRANSLATION_BIN_FILES}
               ${TRANSLATION_QRC})
target_link_libraries(${PROJECT_NAME} openhantek_core ${OPENHANTEK_QT_LIBS} ${OPENGL_LIBRARIES})
openhantek_setup(${PROJECT_NAME})
if(NOT WIN32)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFTW_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} ${FFTW_LIBRARIES})
endif()
include(../cmake/libusb_on_windows.cmake) # Windows only: links libusb into ${PROJECT_NAME}

# --- PSG9080: function generator
add_executable(PSG9080 ${EXECTYPE} src/apps/psg9080_main.cpp src/apps/psg9080window.cpp src/apps/psg9080window.h)
target_link_libraries(PSG9080 psg9080_core ${OPENHANTEK_QT_LIBS})
openhantek_setup(PSG9080)

# --- OpenHantekBode: frequency response
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/bode_main.cpp")
    add_executable(OpenHantekBode ${EXECTYPE} src/apps/bode_main.cpp ${BODE_SRC} ${BODE_HDR})
    target_link_libraries(OpenHantekBode openhantek_core psg9080_core ${OPENHANTEK_QT_LIBS})
    openhantek_setup(OpenHantekBode)
    install(TARGETS OpenHantekBode RUNTIME DESTINATION "bin")
endif()

# install commands
install(TARGETS ${PROJECT_NAME} PSG9080 RUNTIME DESTINATION "bin")

include(../cmake/copy_qt_dlls_to_bin_dir.cmake)

# Unit tests of the parts without Qt (PSG9080 protocol, Bode measurement):
#   cmake -DOPENHANTEK_TESTS=ON .. && make openhantek_tests && ctest
option(OPENHANTEK_TESTS "Build the unit tests" OFF)
if (OPENHANTEK_TESTS)
    enable_testing()
    foreach(T psg9080protocol bodeanalysis)
        add_executable(test_${T} tests/test_${T}.cpp)
        target_include_directories(test_${T} PRIVATE src tests)
        set_target_properties(test_${T} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
        add_test(NAME ${T} COMMAND test_${T})
    endforeach()
    target_sources(test_psg9080protocol PRIVATE src/generator/psg9080protocol.cpp)
    target_sources(test_bodeanalysis PRIVATE src/bode/bodeanalysis.cpp)
    add_custom_target(openhantek_tests DEPENDS test_psg9080protocol test_bodeanalysis)
endif()
