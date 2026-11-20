// Clipboard access with the Wayland round trip handled.
#pragma once

#include <QString>

namespace app {

// Reads the clipboard as plain text. Wayland delivers selection data asynchronously, so a first
// read can be empty although the clipboard holds text; this pumps the event loop for at most
// `waitMs`.
[[nodiscard]] QString readClipboardText(int waitMs);

}  // namespace app
