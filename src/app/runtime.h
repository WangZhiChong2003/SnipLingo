// GUI runtime: owns the long-lived objects, the single-instance guard and the two windows.
//
// Started by `sniplyngo`, `sniplyngo settings` or `sniplyngo trigger <flow>`; when an instance is
// already running the request is forwarded over a local socket and this process exits immediately.
#pragma once

#include "app/cli.h"

namespace app {

// Runs the application window and tray icon. Requires an existing QApplication.
int runGui(const CommandLine &commandLine);

}  // namespace app
