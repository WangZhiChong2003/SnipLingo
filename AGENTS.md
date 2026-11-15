# AGENTS.md

## 1. Project Overview

This project is a free, open-source, Linux-first desktop translation application.

Its primary goal is to let users translate on-screen text with minimal effort, low latency, and minimal interruption to their workflow.

### Core Workflow

1. The user triggers translation through a global keyboard shortcut.
2. The user selects a screen region or provides selected text.
3. The application extracts text, using OCR when necessary.
4. The configured translation engine processes the text.
5. The translated result appears in a lightweight floating window.
6. The user can copy the result, dismiss the window, or initiate another translation.

The application should remain lightweight, responsive, and unobtrusive.

Prioritize reliability and everyday usability over feature count.

## 2. Core Engineering Principles

All development decisions should follow these priorities:

1. **Correctness:** Screen capture, text recognition, translation, and presentation must behave reliably.
2. **User Experience:** Minimize interaction steps, latency, and unnecessary interruptions.
3. **Linux Compatibility:** Support modern Linux desktop environments without relying on environment-specific assumptions.
4. **Performance:** Minimize background resource consumption, startup overhead, and unnecessary data movement.
5. **Privacy and Security:** Protect captured screen content, extracted text, and user credentials.
6. **Maintainability:** Keep implementations straightforward, modular, and easy to understand.

### General Rules

- Prefer simple, direct solutions over elaborate abstractions.
- Avoid premature optimization and speculative extensibility.
- Do not introduce unnecessary compatibility layers, wrappers, or defensive code.
- Fix root causes instead of hiding symptoms behind workarounds.
- Reuse existing implementations instead of creating parallel systems.
- Introduce abstractions only when they solve an actual problem.
- Keep dependencies and background services to a minimum.
- Prefer complete, working implementations over placeholders or speculative scaffolding.

Do not sacrifice simplicity merely to follow a design pattern.

## 3. Platform Support

### 3.1 Linux First

Linux is the primary target platform.

The application must explicitly distinguish between:

- Wayland
- X11

Important environments include:

- GNOME
- KDE Plasma
- Common Wayland compositors
- Multi-monitor configurations
- HiDPI and fractional scaling
- Different display resolutions and coordinate systems
- Light and dark desktop themes

Do not assume that all Linux desktop environments expose identical functionality.

Cross-platform support may be added later, but it must not compromise the Linux experience.

### 3.2 Wayland and X11

Treat screen capture, global shortcuts, window positioning, and text acquisition as platform-dependent capabilities.

Prefer established system interfaces, including:

- XDG Desktop Portal
- D-Bus
- Native desktop APIs where appropriate

When using desktop portals, verify actual interface availability, supported versions, and backend capabilities.

Wayland and X11 may require separate implementations behind a shared interface.

Respect operating-system permission and security boundaries.

Do not bypass compositor security restrictions.

If a feature is unavailable, report the limitation clearly and provide a reasonable fallback when possible.

Never claim Wayland compatibility based solely on successful X11 testing.

### 3.3 Global Shortcuts

Global shortcuts should be configurable.

Requirements:

- Detect and report shortcut registration failures.
- Handle conflicts with existing system shortcuts.
- Respect desktop environment restrictions.
- Avoid requiring elevated privileges.
- Provide a fallback activation mechanism when global shortcuts are unavailable.

Do not assume that all Wayland compositors allow applications to register arbitrary global shortcuts.

## 4. Architecture

Separate platform integration, text extraction, translation, and presentation logic.

Recommended logical modules:

### Capture

Responsible for:

- Screen-region selection
- Screenshot acquisition
- Display geometry and coordinate conversion
- Platform-specific capture backends

### Text Acquisition

Responsible for:

- Retrieving selected text when supported
- OCR processing
- Language detection when applicable
- Text normalization

### Translation

Responsible for:

- Translation engine integration
- Language selection
- Request management
- Timeouts and cancellation
- Translation result processing

### Presentation

Responsible for:

- Selection overlays
- Floating translation windows
- Keyboard and pointer interactions
- Result display and copying

### Settings

Responsible for:

- User preferences
- Keyboard shortcuts
- Translation engine configuration
- Language preferences
- Privacy-related options

These are logical boundaries, not mandatory libraries or processes.

### Architectural Requirements

- Keep OCR independent of the GUI.
- Keep translation engines independent of presentation logic.
- Isolate platform-specific implementations behind explicit interfaces.
- Avoid unnecessary intermediate representations and data copies.
- Do not introduce complex plugin frameworks without concrete requirements.
- Keep data ownership and component lifetimes clear.
- Avoid global mutable state unless there is a clear justification.

Prefer a small number of well-defined components over excessive fragmentation.

## 5. Screen Capture and OCR

### 5.1 Screen Capture

Screen capture must be explicitly initiated by the user.

Requirements:

- Capture only the requested content.
- Handle cancellation immediately.
- Support multiple monitors and display scaling.
- Convert logical and physical coordinates correctly.
- Preserve sufficient image resolution for OCR.
- Avoid unnecessary image encoding and decoding.
- Keep intermediate image data in memory when practical.
- Do not persist screenshots by default.

Respect the capture permissions and capabilities of the active desktop environment.

Avoid unnecessary full-screen capture when an efficient region-capture mechanism is available.

Do not assume region capture is available through every screenshot backend.

### 5.2 OCR

The OCR subsystem must remain independent of the capture and presentation implementations.

Initial language priorities:

- English
- Simplified Chinese
- Mixed Chinese and English text

The architecture should permit additional languages without substantial changes.

Requirements:

- Support configurable recognition languages where practical.
- Handle empty results and recognition failures.
- Avoid unnecessary repeated recognition.
- Minimize model initialization overhead.
- Avoid heavyweight preprocessing unless justified by measurable accuracy improvements.
- Preserve meaningful text structure when possible.

OCR performance should be evaluated using representative screenshots rather than artificial benchmarks alone.

## 6. Translation Engines

Translation engines must be interchangeable without affecting the UI or OCR implementations.

Support for both local and remote translation engines should be considered.

### Requirements

- Configurable source and target languages
- Automatic source-language detection when supported
- Engine selection
- Explicit request timeouts
- Request cancellation
- Network failure handling
- Consistent result representation
- Clear error reporting

Do not assume that external translation services are free or unlimited.

The application itself must remain free and open-source, but external services may require user-provided credentials or incur charges.

### Credentials

- Never hardcode API keys or access tokens.
- Never commit credentials to the repository.
- Prefer secure operating-system credential storage.
- Do not expose credentials in logs or error messages.

### Local Processing

Local OCR and translation backends must not depend on remote services for their own operation.

Do not silently fall back from local processing to a remote service.

Users must understand when their text is transmitted to a third party.

Avoid introducing unnecessary network dependencies.

## 7. Performance and Concurrency

The application should remain responsive even when OCR or translation takes significant time.

### GUI Responsiveness

Never perform expensive or blocking operations on the GUI thread.

Examples include:

- OCR inference
- Network requests
- Model initialization
- Large image transformations
- Blocking file operations

Use appropriate asynchronous mechanisms or worker threads.

### Task Management

- Prefer event-driven execution.
- Avoid polling when event notifications are available.
- Do not use busy waiting.
- Set explicit timeouts for potentially blocking external operations.
- Support cancellation where practical.
- Prevent stale requests from overwriting newer results.
- Stop unnecessary work when the user cancels an operation.
- Avoid redundant background workers and timers.
- Reuse expensive initialized resources when appropriate.
- Define clear ownership and shutdown behavior for asynchronous tasks.

Do not introduce an elaborate scheduling framework for a simple request pipeline.

### Performance Optimization

Optimize measured bottlenecks rather than presumed ones.

Relevant metrics include:

- Idle CPU consumption
- Idle memory usage
- Application startup time
- Shortcut-to-selection latency
- Capture latency
- OCR latency
- Translation overhead
- Time to display the result

Distinguish local processing time from external network latency.

Performance changes should include reproducible measurements when practical.

## 8. User Interface and Interaction

The UI should be minimal, responsive, and consistent with native desktop behavior.

### Core Principles

- Keyboard-first interaction
- Minimal clicks
- Immediate and understandable feedback
- Readable translation results
- Easy copying
- Predictable dismissal and cancellation
- Minimal disruption to other applications

### Interaction Requirements

- Support Escape to cancel or dismiss the current interaction.
- Avoid unnecessarily stealing keyboard focus.
- Position floating windows within visible screen boundaries.
- Handle multi-monitor layouts and fractional scaling.
- Respect system appearance settings when practical.
- Avoid distracting animations and excessive visual effects.
- Do not overwrite clipboard contents unless explicitly requested.

Text selection, clipboard acquisition, and screenshot-based OCR should be treated as distinct acquisition methods.

Do not assume that selected text can always be obtained directly from other applications.

### Accessibility and Localization

- Avoid hardcoded UI strings.
- Keep user-visible text localizable.
- Support appropriate font rendering for multilingual content.
- Prefer standard keyboard navigation and accessibility conventions.
- Do not assume English is the user's preferred language.

## 9. Privacy and Security

Screen captures and extracted text may contain sensitive information.

Privacy protection is a core requirement.

### Mandatory Rules

1. Never capture screen content without a user-initiated action or an explicitly enabled feature.
2. Never transmit screenshots or extracted text without an appropriately configured remote-processing feature.
3. Do not persist screenshots or recognized text by default.
4. Do not collect telemetry or behavioral analytics by default.
5. Do not log full captured content or sensitive credentials.
6. Do not continuously monitor the screen, keyboard, or clipboard by default.
7. Stop capture-related and background tasks when the application exits.
8. Treat all external data and network responses as untrusted.

If translation history, automatic translation, or clipboard monitoring is introduced, provide explicit controls for enabling and disabling these features.

### Security Practices

- Avoid constructing shell commands from untrusted strings.
- Validate external inputs at relevant trust boundaries.
- Handle file and process permissions carefully.
- Avoid running the application with elevated privileges.
- Prefer established system security mechanisms over custom solutions.

Do not add security-related abstractions that provide no meaningful protection.

## 10. Code Quality

### 10.1 General Rules

- Write clear, idiomatic code for the chosen language.
- Follow established repository conventions.
- Keep functions focused on a single responsibility.
- Avoid unnecessarily large files and tightly coupled modules.
- Eliminate duplicated logic where it improves clarity.
- Avoid speculative abstractions.
- Do not retain obsolete compatibility code without a documented requirement.
- Avoid redundant validation and excessive defensive branches.
- Avoid silent failure and empty exception handlers.
- Prefer explicit behavior over hidden side effects.

Do not split straightforward logic into many trivial wrappers.

Do not add comments that merely repeat what the code already expresses.

### 10.2 Error Handling

Handle realistic failures explicitly, including:

- User cancellation
- Permission denial
- Unsupported desktop capabilities
- Empty OCR results
- OCR initialization failure
- Translation timeout
- Network unavailability
- Translation engine errors
- Missing runtime dependencies

Errors should be actionable and accurately reported.

Never fabricate success or suppress errors solely to keep an execution path running.

Avoid unlimited retries.

### 10.3 Logging

Logs should help identify root causes without exposing user data.

- Use appropriate log levels.
- Avoid high-frequency logging in hot paths.
- Avoid duplicate error messages.
- Do not log private screen content or credentials.
- Record operation timings when useful for diagnosis.
- Keep normal operation quiet.

For debugging and command execution, prefer explicit timeouts and captured log output over repeated polling.

## 11. Dependencies and Packaging

Dependencies must be compatible with the project's open-source distribution model.

Evaluate dependencies based on:

- Maintenance status
- License compatibility
- Linux support
- Runtime footprint
- Packaging complexity
- Security considerations
- Long-term maintainability

Avoid heavyweight dependencies for trivial functionality.

Do not require administrator privileges for normal application operation.

Packaging should account for:

- Runtime dependencies
- Desktop entry integration
- Application icons
- Shortcut configuration
- Portal integration
- Permissions
- Distribution-specific limitations

Do not implement multiple packaging formats merely for completeness.

Choose supported distribution formats based on actual project requirements.

## 12. Testing and Verification

Changes must be verifiable.

Prioritize testing for:

- Screen-region coordinate conversion
- Multi-monitor behavior
- HiDPI and fractional scaling
- OCR success, failure, and empty results
- Translation success and failure
- Request timeouts
- Cancellation
- Concurrent and stale requests
- Unsupported platform capabilities
- Global shortcut registration failure
- Clipboard behavior
- Privacy-sensitive operations

### Testing Principles

- Use mock backends for platform-independent logic.
- Use real desktop environments for platform integration tests.
- Test Wayland and X11 separately where relevant.
- Do not treat mocked tests as proof of desktop compatibility.
- Avoid brittle tests that depend unnecessarily on external services.
- Do not modify tests merely to conceal implementation failures.
- Report untested configurations honestly.

Tests should verify observable behavior rather than implementation details whenever practical.

## 13. Agent Development Workflow

Coding agents must follow this workflow for every task.

### Before Editing

1. Read the relevant existing code and documentation.
2. Understand the current architecture and conventions.
3. Identify the actual requirement or root cause.
4. Determine the smallest complete change needed.
5. Check whether an existing implementation can be reused.

### During Editing

1. Keep modifications focused.
2. Preserve established interfaces unless changes are necessary.
3. Avoid introducing parallel implementations.
4. Do not add dependencies without justification.
5. Maintain clear resource ownership.
6. Preserve existing behavior outside the requested scope.
7. Fix underlying problems rather than layering workarounds.

### After Editing

1. Run relevant builds, checks, or tests.
2. Inspect changes for unintended side effects.
3. Verify affected interfaces and module boundaries.
4. Update documentation when necessary.
5. Summarize modifications and verification results.
6. Explicitly disclose remaining limitations.

### Prohibited Practices

- Large-scale refactoring without understanding the existing code
- Unnecessary frameworks or speculative infrastructure
- Infinite polling or unbounded waiting
- Silent error suppression
- Fabricated test results
- Claiming success without verification
- Unnecessary third-party dependencies
- Destructive operations without authorization
- Replacing established implementations without sufficient reason
- Temporary patches that conceal known root causes

Do not create placeholder implementations and present them as completed features.

Favor small, complete, testable changes.

## 14. Documentation and Communication

Use English for:

- AGENTS.md
- Developer documentation
- Code comments
- Commit messages
- Technical design documents

User-facing UI text must remain localizable.

Do not hardcode a specific user language into application logic.

Documentation must accurately reflect implemented behavior.

Clearly distinguish between:

- Implemented
- Tested
- Partially supported
- Planned

Never describe planned functionality as already available.

Keep documentation concise, technical, and maintainable.

## 15. Definition of Done

A task is complete only when:

- The implemented behavior meets the requirement.
- No known critical regressions remain.
- Relevant tests pass, or testing limitations are explicitly reported.
- No unnecessary background resource consumption is introduced.
- Privacy and permission requirements are respected.
- Errors are handled and reported appropriately.
- The code remains consistent with the existing architecture.
- Required documentation is updated.

## 16. Final Guiding Principle

**This project is a fast, reliable, lightweight Linux desktop translation tool—not a general-purpose desktop automation platform.**

Every implementation decision should make the application simpler, faster, more reliable, or easier to maintain.

When multiple solutions are viable, prefer the simplest solution that fully satisfies the actual requirements.