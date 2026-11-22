#pragma once
#include <string>

namespace zy {

// Reads the clipboard's text for pasting into the composition (Ctrl+V while composing).
// Never hangs the app: text copied by another program may be rendered on demand by that program
// (it is asked for the data when someone pastes), so that read runs on a separate thread and is
// given up after a moment if the program does not answer. Returns false if there is no text or it
// could not be read in time.
bool ReadClipboardText(std::wstring* out);

}  // namespace zy
