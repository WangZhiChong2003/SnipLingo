// Engine management from the command line.
//
// The settings window is the primary way to configure an engine, but a headless session, a broken
// UI or a scripted setup must not block the user: these commands operate on the same settings
// document.
#pragma once

#include "app/cli.h"

namespace app {

// All four return a process exit code.
int engineList(const CommandLine &commandLine);
int engineUse(const QString &id);
// Adds a custom OpenAI-compatible engine (a local Ollama server, a company mirror, ...) and
// activates it.
int engineAdd(const QString &id, const QString &baseUrl, const QString &model);
int engineSetTarget(const QString &code);
// Sends a fixed word through one engine (or every usable one) and reports what came back.
int engineTest(const QString &id, bool all);
// `sniplyngo proxy [mode [url]]`; empty mode only reports.
int proxyCommand(const QString &mode, const QString &url);

}  // namespace app
