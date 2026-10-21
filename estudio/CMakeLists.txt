cmake_minimum_required(VERSION 3.22)

# Racoon: el DAW de la familia Racoon. Motor: Tracktion Engine (GPLv3) sobre JUCE.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(RACOON VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# tracktion_engine (v3.2.0) y su JUCE fijado viven en ./tracktion_engine (los descarga scripts/build_mac.sh)
add_subdirectory(tracktion_engine/modules/juce)
add_subdirectory(tracktion_engine/modules)

# Todo el código compartido (módulos JUCE/Tracktion + motor + UI) se compila una sola vez
# en una biblioteca estática; la app y el arnés de pruebas la enlazan.
add_library(racoon_core STATIC)

juce_add_binary_data(RacoonData SOURCES
    Resources/fonts/DelaGothicOne.ttf
    Resources/fonts/AlumniSans-ExtraBold.ttf
    Resources/fonts/GolosText-Regular.ttf
    Resources/fonts/GolosText-SemiBold.ttf
    Resources/fonts/GolosText-Medium.ttf
    Resources/fonts/JetBrainsMono-Medium.ttf
    Resources/fonts/JetBrainsMono-SemiBold.ttf
    Resources/animals/racoon.png
    Resources/animals/badger.png
    Resources/animals/hamster.png
    Resources/animals/hedgehog.png
    Resources/animals/skunk.png
    Resources/animals/mole.png
    Resources/animals/porcupine.png
    Resources/animals/ferret.png
    Resources/animals/chinchilla.png
    Resources/animals/dormouse.png
    Resources/animals/squirrel.png
    Resources/animals/chipmunk.png)
set_target_properties(RacoonData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

file(GLOB_RECURSE RACOON_CORE CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/fx/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/fx/*.h)
target_sources(racoon_core PRIVATE ${RACOON_CORE})
target_include_directories(racoon_core PUBLIC Source)

target_compile_definitions(racoon_core PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    RACOON_VERSION_STRING="${PROJECT_VERSION}")

# Muestras Racoon: la carpeta muestras del paquete (al lado de estudio y plugins); solo la usa Biblioteca.cpp
set(RACOON_MUESTRAS_DEV "${CMAKE_CURRENT_SOURCE_DIR}/../muestras")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Source/engine/Biblioteca.cpp PROPERTIES COMPILE_DEFINITIONS "RACOON_MUESTRAS_DEV=\"${RACOON_MUESTRAS_DEV}\"")

target_link_libraries(racoon_core
    PRIVATE
        RacoonData
        tracktion::tracktion_core
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

# los consumidores necesitan las rutas y definiciones de los módulos, sin recompilarlos
target_include_directories(racoon_core INTERFACE
    $<TARGET_PROPERTY:racoon_core,INCLUDE_DIRECTORIES>)
target_compile_definitions(racoon_core INTERFACE
    $<TARGET_PROPERTY:racoon_core,COMPILE_DEFINITIONS>)
set_target_properties(racoon_core PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE
    VISIBILITY_INLINES_HIDDEN TRUE
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

if(APPLE)
    target_compile_options(racoon_core PRIVATE "-fno-aligned-allocation")
endif()

# Separar en stems: demucs.cpp (MIT) sobre Eigen 3.4 (MPL 2.0), en su propia biblioteca y con sus propias banderas.
# Racoon solo ve ThirdParty/demucs.cpp/racoon/StemsCore.h (vectores de float, nada de Eigen).
file(GLOB RACOON_DEMUCS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/demucs.cpp/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/demucs.cpp/racoon/*.cpp)
add_library(racoon_stems STATIC ${RACOON_DEMUCS})
target_include_directories(racoon_stems PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/eigen
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/demucs.cpp/src)
target_include_directories(racoon_stems PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/demucs.cpp/racoon)
set_target_properties(racoon_stems PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE TRUE)
# rapido aunque se compile en Debug: sin esto una cancion tarda horas
target_compile_options(racoon_stems PRIVATE -O3 -DNDEBUG -ffast-math -fno-finite-math-only -w)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(racoon_stems PRIVATE pthread)
endif()
target_link_libraries(racoon_core PUBLIC racoon_stems)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(racoon_core PUBLIC "-latomic")
endif()

juce_add_gui_app(Racoon
    PRODUCT_NAME "Racoon"
    COMPANY_NAME "Racoon"
    BUNDLE_ID "com.racoon.daw"
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_256.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Racoon usa el microfono o tu interfaz de audio para grabar."
    DOCUMENT_EXTENSIONS racoon
    PLUGINHOST_AU TRUE
    HARDENED_RUNTIME_ENABLED FALSE)

target_sources(Racoon PRIVATE Source/Main.cpp)
target_link_libraries(Racoon PRIVATE racoon_core)

# arnés de pruebas sin ventana (Linux CI / desarrollo)
option(RACOON_TESTS "Build the Racoon test harness" OFF)
if(RACOON_TESTS)
    juce_add_console_app(RacoonTest PRODUCT_NAME "RacoonTest")
    target_sources(RacoonTest PRIVATE Tests/RacoonTest.cpp Tests/UiTests.cpp Tests/ToolsTests.cpp)
    target_compile_definitions(RacoonTest PRIVATE RACOON_TEST_HARNESS=1 RACOON_WITH_UI_TESTS=1)
    target_link_libraries(RacoonTest PRIVATE racoon_core)

    # efectos propios: contrato comun y pruebas de cada uno, sin ventana
    juce_add_console_app(FxTest PRODUCT_NAME "FxTest")
    target_sources(FxTest PRIVATE Tests/fx/FxTest.cpp)
    target_include_directories(FxTest PRIVATE Tests/fx)
    target_compile_definitions(FxTest PRIVATE RACOON_TEST_HARNESS=1)
    target_link_libraries(FxTest PRIVATE racoon_core)
endif()
