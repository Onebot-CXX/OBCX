## Why

The fullscreen TUI currently treats Ctrl+C as exit, conflicting with familiar
selection-and-copy behavior and making accidental shutdown easy. Users running
OBCX through SSH and tmux need selected output copied to their client machine's
clipboard, not to a graphical clipboard on the server.

## What Changes

- **BREAKING** In the fullscreen TUI, Ctrl+C copies an active text selection;
  without a selection it clears command input and never requests shutdown.
- Ctrl+D requests a process-directed SIGINT only when command input is empty,
  reusing the existing graceful shutdown path. Nonempty input does not exit.
  External SIGINT/SIGTERM, the explicit exit command and non-TUI operation remain
  unchanged.
- Add mouse text selection and highlighting for log and CLI output, coordinated
  with existing scrollbar and divider interactions. Selection remains tied to
  the selected content rather than shifting when logs arrive or layout changes.
- Explicit Ctrl+C copy emits a bounded, write-only OSC 52 request through the
  terminal stream, with tmux transport handling and documented SSH/tmux/terminal
  prerequisites. No clipboard read, remote GUI utility or automatic copy on
  selection/log updates is introduced.
- Preserve source text across soft wraps, UTF-8 glyph boundaries and explicit
  newlines; exclude pane borders, colors and scrollbar decoration from copying.
- Provide content-safe feedback for unavailable/over-limit copy operations and
  distinguish a request being emitted from verified clipboard success.
- Document the new keys and interoperability limits. Introduce no new application
  configuration options/defaults, unit tests, deployment or terminal/tmux config
  mutation.

## Capabilities

### New Capabilities

- `tui-input-and-clipboard`: selection-aware Ctrl+C, empty-input Ctrl+D SIGINT,
  bounded source-text copying and client-terminal clipboard delivery over OSC 52.

### Modified Capabilities

- `responsive-tui-layout`: selection-aware viewport anchoring and interaction
  with new output, resize/reflow, eviction, scrollbar navigation and pane sizing.

## Impact

Core only: `src/tui/tui_app.cpp`, `include/tui/tui_app.hpp`, TUI layout/text mapping
helpers, process interrupt wiring in `src/app/main.cpp` / CLI context as needed,
and TUI user documentation. Reuse the installed FTXUI 6.1.9 selection facilities
where they satisfy the source-identity and copy requirements. No actor, bot SDK,
database, provider protocol, clipboard daemon or new third-party dependency is
part of this change. Terminal and tmux settings remain operator-owned.
