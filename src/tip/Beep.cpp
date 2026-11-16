#include "tip/Beep.h"

#include <windows.h>
#include <mmsystem.h>

#include <atomic>

#include "tip/Globals.h"

namespace zy {
namespace {

std::atomic<bool> g_playing{false};

void CALLBACK BeepCallback(PTP_CALLBACK_INSTANCE, void*) {
    // Synchronous on this pool thread (about 40 ms). winmm is delay-loaded, so processes that
    // never beep never load it. SND_NODEFAULT: if the resource is missing, stay silent.
    __try {
        PlaySoundW(MAKEINTRESOURCEW(kBeepResource), g_hInst, SND_RESOURCE | SND_SYNC | SND_NODEFAULT);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // winmm.dll could not be loaded (delay-load failure): no sound.
    }
    g_playing = false;
}

}  // namespace

void PlayErrorBeep() {
    if (g_playing.exchange(true)) return;
    TP_CALLBACK_ENVIRON env;
    InitializeThreadpoolEnvironment(&env);
    // Keeps our DLL loaded until the callback has returned, even if the host unloads the IME.
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&BeepCallback), &self) &&
        self)
        SetThreadpoolCallbackLibrary(&env, self);
    if (!TrySubmitThreadpoolCallback(&BeepCallback, nullptr, &env)) g_playing = false;
    DestroyThreadpoolEnvironment(&env);
}

}  // namespace zy
