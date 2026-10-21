cmake_minimum_required(VERSION 3.22)

# La familia Racoon: nueve plugins que comparten el mismo marco (tema, presets, A/B, editor, instalador).
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(RACOON_FAMILY VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

# Muestras Racoon (muestras con licencia libre) va al lado de esta carpeta en el paquete; los plugins también la buscan
# en Música/Racoon/Muestras Racoon y en RACOON_MUESTRAS. La ruta del paquete solo la usa Shared/Biblioteca.cpp.
set(RF_MUESTRAS_DEV "${CMAKE_CURRENT_SOURCE_DIR}/../muestras")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Shared/Biblioteca.cpp PROPERTIES COMPILE_DEFINITIONS "RACOON_MUESTRAS_DEV=\"${RF_MUESTRAS_DEV}\"")

set(RF_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND RF_FORMATS AU)
endif()

# Shared: lo que tambien usan Badger y Hamster (la automatizacion)
file(GLOB RF_SHARED CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Shared/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Shared/*.h)
file(GLOB RF_COMMON CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Common/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Common/*.h)
list(APPEND RF_COMMON ${RF_SHARED})
set(RF_FONTS
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/DelaGothicOne.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/AlumniSans-ExtraBold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/GolosText-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/GolosText-SemiBold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/GolosText-Medium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/JetBrainsMono-Medium.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/JetBrainsMono-SemiBold.ttf)
set(RF_DEFS
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0 JUCE_MODAL_LOOPS_PERMITTED=1)
set(RF_WARN -Wno-unused-parameter -Wno-shadow -Wno-sign-conversion -Wno-float-conversion -Wno-implicit-float-conversion
    -Wno-conversion -Wno-shorten-64-to-32 -Wno-unused-variable -Wno-switch-enum -Wno-switch -Wno-old-style-cast)

set(RF_ALL_PLUGINS "")

# racoon_plugin(<Animal> CODE <4 chars> KIND effect|instrument|midifx [SIDECHAIN] [MIDI_IN] CATEGORIES ...)
function(racoon_plugin NAME)
    cmake_parse_arguments(P "SIDECHAIN;MIDI_IN;MIDI_OUT" "CODE;KIND;DESC;SUFFIX" "CATEGORIES" ${ARGN})
    string(TOLOWER ${NAME} lname)
    set(target Racoon${NAME})
    set(is_synth FALSE)
    set(midi_in ${P_MIDI_IN})
    set(midi_out FALSE)
    if(P_MIDI_OUT)
        set(midi_out TRUE)
    endif()
    set(midi_fx FALSE)
    set(au_type kAudioUnitType_Effect)
    if(P_KIND STREQUAL "instrument")
        set(is_synth TRUE)
        set(midi_in TRUE)
        set(au_type kAudioUnitType_MusicDevice)
    elseif(P_KIND STREQUAL "midifx")
        set(midi_in TRUE)
        set(midi_out TRUE)
        set(midi_fx TRUE)
        set(au_type kAudioUnitType_MIDIProcessor)
    elseif(midi_in)
        set(au_type kAudioUnitType_MusicEffect)
    endif()
    if(NOT midi_in)
        set(midi_in FALSE)
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME "Racoon"
        COMPANY_WEBSITE "https://doceveinte.com"
        BUNDLE_ID "com.racoon.${lname}"
        PLUGIN_MANUFACTURER_CODE Dv20
        PLUGIN_CODE ${P_CODE}
        FORMATS ${RF_FORMATS}
        PRODUCT_NAME "Racoon - ${NAME} ${P_SUFFIX}"
        VERSION ${PROJECT_VERSION}
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icons/${lname}_1024.png"
        ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icons/${lname}_256.png"
        DESCRIPTION "${P_DESC}"
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT ${midi_in}
        NEEDS_MIDI_OUTPUT ${midi_out}
        IS_MIDI_EFFECT ${midi_fx}
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES ${P_CATEGORIES}
        AU_MAIN_TYPE ${au_type}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Para procesar el audio de tu interfaz cuando se usa como app independiente."
        HARDENED_RUNTIME_ENABLED FALSE)
    juce_generate_juce_header(${target})
    juce_add_binary_data(${target}Data SOURCES ${RF_FONTS} ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icons/${lname}_256.png)
    file(GLOB plugin_src CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/${NAME}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/${NAME}/*.h)
    target_sources(${target} PRIVATE ${RF_COMMON} ${plugin_src})
    target_include_directories(${target} PRIVATE Common Plugins/${NAME} Shared)
    target_compile_definitions(${target} PUBLIC ${RF_DEFS})
    target_link_libraries(${target}
        PRIVATE ${target}Data juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE ${RF_WARN})
    endif()
    set(RF_ALL_PLUGINS ${RF_ALL_PLUGINS} ${NAME} PARENT_SCOPE)
endfunction()

set(RF_ONLY "" CACHE STRING "Build only this plugin (empty: all)")

macro(maybe_plugin NAME)
    if(NOT RF_ONLY OR "${NAME}" IN_LIST RF_ONLY)
        racoon_plugin(${NAME} ${ARGN})
    endif()
endmacro()

maybe_plugin(Dormouse   CODE RcDm SUFFIX "Pads" KIND instrument DESC "Sintetizador de pads"       CATEGORIES Instrument Synth)
maybe_plugin(Squirrel   CODE RcSq SUFFIX "Sampler" KIND instrument DESC "Sampler"                    CATEGORIES Instrument Sampler)
maybe_plugin(Chipmunk   CODE RcCp SUFFIX "Chords" KIND instrument MIDI_OUT DESC "Acordes y arpegios"    CATEGORIES Instrument Synth)
maybe_plugin(Mole       CODE RcMl SUFFIX "Rhythm" KIND instrument MIDI_OUT DESC "Groove y ritmo"        CATEGORIES Instrument Drum)
maybe_plugin(Skunk      CODE RcSn SUFFIX "Sound Design" KIND effect MIDI_IN SIDECHAIN DESC "Diseno sonoro" CATEGORIES Fx)
maybe_plugin(Ferret     CODE RcFr SUFFIX "Guitar" KIND instrument DESC "Laboratorio de guitarra" CATEGORIES Instrument)
maybe_plugin(Porcupine  CODE RcPr SUFFIX "Choir" KIND instrument DESC "Coro y ensamble" CATEGORIES Instrument)
maybe_plugin(Hedgehog   CODE RcHg SUFFIX "Mastering" KIND effect DESC "Laboratorio de mastering" CATEGORIES Fx Mastering)
maybe_plugin(Chinchilla CODE RcCn SUFFIX "Vocals" KIND effect MIDI_IN SIDECHAIN DESC "Laboratorio vocal" CATEGORIES Fx)

# Badger (bajos) y Hamster (bateria): tienen su propio codigo y sus fuentes, pero se compilan aqui con los demas
# (mismos codigos y bundle id que antes: instalarlos reemplaza las versiones anteriores)
function(engine_plugin NAME SUFFIX CODE TARGET_DESC VST3CAT)
    set(target Racoon${NAME})
    set(dir ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/${NAME})
    string(TOLOWER ${NAME} lname)
    juce_add_plugin(${target}
        COMPANY_NAME "Racoon"
        COMPANY_WEBSITE "https://doceveinte.com"
        BUNDLE_ID "com.racoon.${lname}"
        PLUGIN_MANUFACTURER_CODE Dv20
        PLUGIN_CODE ${CODE}
        FORMATS ${RF_FORMATS}
        PRODUCT_NAME "Racoon - ${NAME} ${SUFFIX}"
        VERSION 2.0.0
        ICON_BIG "${dir}/Resources/icon/icon_1024.png"
        ICON_SMALL "${dir}/Resources/icon/icon_256.png"
        DESCRIPTION "${TARGET_DESC}"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument ${VST3CAT}
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        MICROPHONE_PERMISSION_ENABLED FALSE
        HARDENED_RUNTIME_ENABLED FALSE)
    juce_generate_juce_header(${target})
    juce_add_binary_data(${target}Data SOURCES
        ${dir}/Resources/fonts/DelaGothicOne.ttf
        ${dir}/Resources/fonts/AlumniSans-SemiBold.ttf
        ${dir}/Resources/fonts/AlumniSans-ExtraBold.ttf
        ${dir}/Resources/fonts/GolosText-Regular.ttf
        ${dir}/Resources/fonts/GolosText-SemiBold.ttf
        ${dir}/Resources/fonts/GolosText-Medium.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/JetBrainsMono-Medium.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/JetBrainsMono-SemiBold.ttf)
    file(GLOB_RECURSE src CONFIGURE_DEPENDS ${dir}/Source/*.cpp ${dir}/Source/*.h)
    target_sources(${target} PRIVATE ${src} ${RF_SHARED})
    target_include_directories(${target} PRIVATE ${dir}/Source ${CMAKE_CURRENT_SOURCE_DIR}/Shared)
    target_compile_definitions(${target} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
                               JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(${target}
        PRIVATE ${target}Data juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wno-unused-parameter -Wno-shadow -Wno-sign-conversion -Wno-float-conversion
            -Wno-implicit-float-conversion -Wno-conversion -Wno-shorten-64-to-32 -Wno-unused-variable -Wno-switch-enum -Wno-switch -Wno-old-style-cast)
    endif()
endfunction()

if(NOT RF_ONLY OR "Badger" IN_LIST RF_ONLY)
    engine_plugin(Badger Bass BsEn "Bass synthesizer" Synth)
endif()
if(NOT RF_ONLY OR "Hamster" IN_LIST RF_ONLY)
    engine_plugin(Hamster Drums DrEn "Drum machine" Drum)
    target_compile_definitions(RacoonHamster PUBLIC RACOON_HAMSTER=1)
endif()

# Pruebas sin ventana: todos los procesadores en un solo programa (no se distribuye)
option(RF_TESTS "Build the family test harness" OFF)
if(RF_TESTS)
    juce_add_console_app(FamilyTest PRODUCT_NAME "FamilyTest")
    juce_generate_juce_header(FamilyTest)
    set(test_src ${RF_COMMON} ${CMAKE_CURRENT_SOURCE_DIR}/Tests/FamilyTest.cpp)
    set(test_icons "")
    foreach(p ${RF_ALL_PLUGINS})
        file(GLOB s CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/${p}/*.cpp)
        list(FILTER s EXCLUDE REGEX ".*/Entry\\.cpp$")
        list(APPEND test_src ${s})
        string(TOLOWER ${p} lp)
        list(APPEND test_icons ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icons/${lp}_256.png)
        target_include_directories(FamilyTest PRIVATE Plugins/${p})
    endforeach()
    juce_add_binary_data(FamilyTestData SOURCES ${RF_FONTS} ${test_icons})
    target_sources(FamilyTest PRIVATE ${test_src})
    target_include_directories(FamilyTest PRIVATE Common Plugins Shared)
    target_compile_definitions(FamilyTest PRIVATE ${RF_DEFS} RF_TEST_HARNESS=1 DOCEVEINTE_INSTALLER_TEST=1
        JucePlugin_Name="RacoonTest" JucePlugin_Build_Standalone=0)
    target_link_libraries(FamilyTest PRIVATE FamilyTestData juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
    if(NOT MSVC)
        target_compile_options(FamilyTest PRIVATE -w)
    endif()
endif()

# Pruebas de Badger y Hamster (su automatizacion y su exportacion), un programa por plugin
if(RF_TESTS)
    foreach(E Badger Hamster)
        set(dir ${CMAKE_CURRENT_SOURCE_DIR}/Plugins/${E})
        string(TOUPPER ${E} UE)
        juce_add_console_app(${E}Test PRODUCT_NAME "${E}Test")
        juce_generate_juce_header(${E}Test)
        file(GLOB_RECURSE esrc CONFIGURE_DEPENDS ${dir}/Source/*.cpp)
        target_sources(${E}Test PRIVATE ${esrc} ${RF_SHARED} ${CMAKE_CURRENT_SOURCE_DIR}/Tests/EngineTest.cpp)
        target_include_directories(${E}Test PRIVATE ${dir}/Source ${CMAKE_CURRENT_SOURCE_DIR}/Shared)
        target_compile_definitions(${E}Test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
            ENGINE_${UE}=1 JucePlugin_Name="Racoon - ${E}" JucePlugin_Build_Standalone=0 JucePlugin_VersionString="2.0.0"
            JucePlugin_Manufacturer="Racoon" DOCEVEINTE_INSTALLER_TEST=1)
        if(E STREQUAL "Hamster")
            target_compile_definitions(${E}Test PRIVATE RACOON_HAMSTER=1)
        endif()
        target_link_libraries(${E}Test PRIVATE Racoon${E}Data juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
            juce::juce_recommended_config_flags)
        if(NOT MSVC)
            target_compile_options(${E}Test PRIVATE -w)
        endif()
    endforeach()
endif()
