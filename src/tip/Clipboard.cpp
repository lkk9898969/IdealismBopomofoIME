#include "tip/Clipboard.h"

#include <windows.h>

#include <memory>

namespace zy {
namespace {

constexpr DWORD kTimeoutMs = 200;    // longest the key handler waits for another program
constexpr size_t kMaxChars = 1 << 20;  // ignore anything beyond (a paste into a sentence)

bool ReadNow(std::wstring* out) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(nullptr)) return false;
    bool ok = false;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
            const size_t capacity = GlobalSize(data) / sizeof(wchar_t);
            size_t n = 0;
            while (n < capacity && n < kMaxChars && text[n]) ++n;
            out->assign(text, n);
            GlobalUnlock(data);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

struct Job {
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HMODULE module = nullptr;  // a reference on our DLL, held while the thread runs
    std::wstring text;
    bool ok = false;
    ~Job() {
        if (done) CloseHandle(done);
    }
};

DWORD WINAPI ReadThread(void* param) {
    HMODULE module = nullptr;
    {
        auto* holder = static_cast<std::shared_ptr<Job>*>(param);
        std::shared_ptr<Job> job = std::move(*holder);
        delete holder;
        module = job->module;
        std::wstring text;
        const bool ok = ReadNow(&text);  // may wait for the program that copied
        job->text = std::move(text);
        job->ok = ok;
        SetEvent(job->done);
    }  // the job goes away here (or with the key handler, whichever is last)
    // May run long after the key handler gave up, even after the IME was released: our DLL stays
    // loaded until this thread is done with it.
    if (module) FreeLibraryAndExitThread(module, 0);
    return 0;
}

}  // namespace

bool ReadClipboardText(std::wstring* out) {
    out->clear();
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
    // Copied in this same thread (or nobody owns it): reading only ever waits for this thread's own
    // window procedure, as when the app pastes by itself.
    const HWND owner = GetClipboardOwner();
    if (!owner || GetWindowThreadProcessId(owner, nullptr) == GetCurrentThreadId()) return ReadNow(out);

    auto job = std::make_shared<Job>();
    if (!job->done) return false;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&ReadThread),
                            &job->module))
        return false;
    auto* holder = new (std::nothrow) std::shared_ptr<Job>(job);
    HANDLE thread = holder ? CreateThread(nullptr, 0, &ReadThread, holder, 0, nullptr) : nullptr;
    if (!thread) {
        delete holder;
        FreeLibrary(job->module);
        return false;
    }
    CloseHandle(thread);
    if (WaitForSingleObject(job->done, kTimeoutMs) != WAIT_OBJECT_0) return false;  // keep typing
    *out = std::move(job->text);
    return job->ok;
}

}  // namespace zy
