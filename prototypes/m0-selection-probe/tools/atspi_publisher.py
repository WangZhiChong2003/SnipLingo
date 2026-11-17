#!/usr/bin/python3
"""AT-SPI publisher for the M0 probe: proves the read path without any human selection.

A GTK3 window whose text view selects a different word every ~0.9 s, printing exactly what it
selected. GTK ships the AT-SPI bridge in the system libraries, so this is a known-good provider:

  * if the watcher reports text for these ranges, our event/read/latency path is correct and any
    application that yields nothing is the application's limitation, not our bug;
  * if the watcher reports zero-length ranges against this provider, our read logic is wrong.

Run it in the session (needs a display), concurrently with:
  snip-atspi-probe watch --duration N --show-text --retry-ms 200

No selection content from other applications is involved: the text is this literal sample.
"""
import sys

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GLib, Gtk  # noqa: E402

TEXT = "alpha beta gamma delta epsilon zeta eta theta iota kappa"
WORDS = TEXT.split(" ")


class Publisher(Gtk.Window):
    def __init__(self, duration_ms: int) -> None:
        super().__init__(title="snip-selection-publisher")
        self.set_default_size(420, 140)
        self.view = Gtk.TextView()
        self.view.set_wrap_mode(Gtk.WrapMode.WORD)
        self.view.set_editable(False)
        self.view.set_cursor_visible(True)
        self.view.get_buffer().set_text(TEXT)
        self.add(self.view)
        self.show_all()

        self.step = 0
        GLib.timeout_add(900, self.select_next)
        GLib.timeout_add(duration_ms, Gtk.main_quit)

    def select_next(self) -> bool:
        index = self.step % (len(WORDS) - 1) + 1
        start = len(" ".join(WORDS[:index])) + 1
        end = start + len(WORDS[index])
        buffer = self.view.get_buffer()
        buffer.select_range(
            buffer.get_iter_at_offset(start), buffer.get_iter_at_offset(end)
        )
        print(
            f"publisher: selected [{start},{end}) len={end - start} text={WORDS[index]!r}",
            flush=True,
        )
        self.step += 1
        return True


def main() -> int:
    duration_ms = int(sys.argv[1]) if len(sys.argv) > 1 else 12000
    GLib.set_prgname("snip-selection-publisher")
    GLib.set_application_name("snip-selection-publisher")
    print(f"publisher: {duration_ms} ms, {len(WORDS) - 1} successive selections", flush=True)
    Publisher(duration_ms)
    Gtk.main()
    print("publisher: done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
