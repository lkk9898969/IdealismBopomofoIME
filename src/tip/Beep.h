#pragma once

namespace zy {

// Resource id of beep.wav (type WAVE) in IdealismTip.rc.
inline constexpr int kBeepResource = 201;

// The error beep (no such syllable), like Microsoft Bopomofo's "嘟". Returns at once: the sound is
// played on the thread pool, never on the key handler's thread. A beep while one is still playing
// is dropped.
void PlayErrorBeep();

}  // namespace zy
