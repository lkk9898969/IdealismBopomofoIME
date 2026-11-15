cmake_minimum_required(VERSION 3.25)
project(IdealismIME LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT: the TIP DLL is loaded into arbitrary host processes (3ds Max, games, old apps),
# so it must not depend on whichever VC runtime the host happens to ship.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/utf-8 /permissive- /EHsc)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# ---- McBopomofo engine (MIT, third_party/McBopomofo, unmodified) ----
set(MCB_ENGINE ${CMAKE_SOURCE_DIR}/third_party/McBopomofo/Source/Engine)
add_library(mcbopomofo_engine STATIC
    ${MCB_ENGINE}/gramambular2/reading_grid.cpp
    ${MCB_ENGINE}/AssociatedPhrasesV2.cpp
    ${MCB_ENGINE}/ByteBlockBackedDictionary.cpp
    ${MCB_ENGINE}/McBopomofoLM.cpp
    ${MCB_ENGINE}/ParselessLM.cpp
    ${MCB_ENGINE}/ParselessPhraseDB.cpp
    ${MCB_ENGINE}/PhraseReplacementMap.cpp
    ${MCB_ENGINE}/UTF8Helper.cpp
    ${MCB_ENGINE}/UserOverrideModel.cpp
    ${MCB_ENGINE}/UserPhrasesLM.cpp
    # Windows replacement for the POSIX MemoryMappedFile.cpp
    src/engine/win/McbMemoryMappedFile.cpp
)
target_include_directories(mcbopomofo_engine PUBLIC ${MCB_ENGINE})
target_include_directories(mcbopomofo_engine PRIVATE src/compat)  # empty unistd.h / sys/mman.h shims
target_compile_options(mcbopomofo_engine PRIVATE /W3 /wd4244 /wd4267)

# ---- Our engine layer: pure C++, no TSF dependency, unit-testable ----
add_library(idealism_engine STATIC
    src/engine/Bopomofo.cpp
    src/engine/Composer.cpp
    src/engine/Dictionary.cpp
    src/engine/EngineHost.cpp
    src/engine/Files.cpp
    src/engine/Learner.cpp
    src/engine/LearningModel.cpp
    src/engine/Utf.cpp
)
target_include_directories(idealism_engine PUBLIC src)
target_link_libraries(idealism_engine PUBLIC mcbopomofo_engine)
target_compile_options(idealism_engine PRIVATE /W4)

# ---- TSF text service: core objects (shared by the DLL and the smoke test) ----
add_library(idealism_tip_core OBJECT
    src/tip/Globals.cpp
    src/tip/ClassFactory.cpp
    src/tip/Register.cpp
    src/tip/TextService.cpp
    src/tip/KeyHandler.cpp
    src/tip/Composition.cpp
    src/tip/Compartment.cpp
    src/tip/CandidateWindow.cpp
    src/tip/CandidateUI.cpp
    src/tip/LangBarButton.cpp
)
target_link_libraries(idealism_tip_core PUBLIC idealism_engine ole32 oleaut32 advapi32 user32 gdi32 uuid)
target_compile_options(idealism_tip_core PRIVATE /W4)

# ---- TSF text service DLL ----
add_library(IdealismTip SHARED
    src/tip/DllMain.cpp
    src/tip/IdealismTip.def
    src/tip/IdealismTip.rc
)
target_link_libraries(IdealismTip PRIVATE idealism_tip_core)
target_compile_options(IdealismTip PRIVATE /W4)

# ---- TIP smoke test: activate/deactivate against a real TSF thread manager, feed keys ----
add_executable(tip_smoke tests/tip_smoke.cpp)
target_link_libraries(tip_smoke PRIVATE idealism_tip_core)
target_compile_options(tip_smoke PRIVATE /W4)

# ---- Engine tests ----
enable_testing()
add_executable(engine_test tests/engine_test.cpp)
target_link_libraries(engine_test PRIVATE idealism_engine)
target_compile_options(engine_test PRIVATE /W4)
add_test(NAME engine_test COMMAND engine_test ${CMAKE_SOURCE_DIR}/data/data.txt)

# ---- Settings app: phrase packs, user/excluded phrases, learning (plain Win32, static CRT) ----
add_executable(IdealismSettings WIN32
    src/settings/SettingsApp.cpp
    src/settings/Settings.rc
    src/settings/settings.manifest
)
target_link_libraries(IdealismSettings PRIVATE idealism_engine comctl32 comdlg32 shell32)
target_compile_options(IdealismSettings PRIVATE /W4)

# ---- 輸入法整合器 (IME pad): handwriting + symbol picker ----
add_library(idealism_pad_core STATIC
    src/pad/Handwriting.cpp
)
target_include_directories(idealism_pad_core PUBLIC src)
target_link_libraries(idealism_pad_core PUBLIC ole32 oleaut32)
target_compile_options(idealism_pad_core PRIVATE /W4)

add_executable(pad_test tests/pad_test.cpp)
target_link_libraries(pad_test PRIVATE idealism_pad_core)

add_executable(IdealismPad WIN32
    src/pad/PadApp.cpp
    src/pad/Pad.rc
    src/settings/settings.manifest
)
target_link_libraries(IdealismPad PRIVATE idealism_pad_core idealism_engine shell32)
target_compile_options(IdealismPad PRIVATE /W4)
