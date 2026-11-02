# Implementation and acceptance record

## Integration inspection

- FTXUI 6.1.9 installed headers and `screen_interactive.cpp.o` were inspected (no validation program). `HandleTask` dispatches to the root component before `HandleSelection` and default Ctrl+C handling; a handled event plus `ForceHandleCtrlC(false)` suppresses the default interrupt. `HandleSelection` clears native coordinate selection when a component handles an event. `GetSelection` returns rendered selection content, not retained entry identity/source offsets. `SelectionChange` therefore cannot establish source retention or distinguish hard newlines from wrapped DOM rows.
- Output selection uses a minimal source adapter, FTXUI glyph/cell mapping, `reflect` hit boxes, standard DOM highlight decorators and `ScreenInteractive::CaptureMouse`. The per-frame hit map stores source positions/boxes only; it reads the existing source under its owner lock instead of retaining another copy of output history. Native screen-coordinate selection is not used as clipboard authority. Existing divider/input components retain their own event handling; no alternative editor or additional scrollback is introduced.
- Logs retain sequence IDs and their original plain text in the bounded wrapped cache. Wrapped rows carry source byte offsets; viewport reflow anchors use source byte positions instead of unstable segment numbers. Console entry indices refer to the existing append-only command/output vector. Clipboard extraction joins original entry slices with entry newlines, never DOM row separators.
- Inspection of FTXUI's `string.cpp.o` also confirmed that `Utf8ToGlyphs` omits controls, emits wide-cell placeholders and can append combining suffixes into those placeholders. The shared source-glyph projection preserves original byte slices (including tabs), folds zero-width continuations into the base, and is used consistently for wrapping and hit testing. Inspection of `frame.cpp.o` established the center focus calculation used to pin console source rows.
- `SignalMonitor` blocks SIGINT/SIGTERM before workers and waits using `sigtimedwait`. The TUI receives an explicit process-owned interrupt callback from `main.cpp`; that callback uses `kill(getpid(), SIGINT)`, not a thread-directed signal or a stop-flag replacement.

## Verification boundary

During implementation/build verification, no new unit tests or validation programs were added or run. No live-process interaction, provider calls, terminal/tmux configuration changes, deployment, restart, push or archive was performed. Repository commits are a separate, explicitly requested operation.

## Source implementation

- `include/tui/tui_selection.hpp`, `src/tui/tui_selection.cpp`: source-backed contiguous selection, FTXUI highlighting/hit boxes, bounded extraction and validated UTF-8/Base64 OSC 52 writer. Nonempty `TMUX` selects exactly one escaped DCS envelope. Only the root Ctrl+C path invokes the writer, on the owning interactive screen's event-loop thread; output is one complete `std::cout.write` followed by flush, not a renderer/log callback or restored-I/O excursion.
- `include/tui/tui_layout.hpp`, `src/tui/tui_layout.cpp`: source offsets, byte-based reflow anchors, source glyph/continuation projection, and explicit pause of log tail-follow. Collection and original retention continue; pinned views do not resume tail-follow merely because offset is temporarily zero.
- `include/tui/tui_sink.hpp`: remove CSI/OSC/control strings and non-whitespace C0 controls from display/source text; OSC supports BEL/ST while other control strings require ST. Clipboard validation also rejects invalid UTF-8 and remaining C0/C1 controls (except source tab/newline).
- `include/tui/tui_app.hpp`, `src/tui/tui_app.cpp`, `src/app/main.cpp`: root-priority copy-or-clear Ctrl+C, exactly-empty-only process SIGINT Ctrl+D, successful-request deduplication, bounded failure feedback, selection retention checks before copy, and mouse/navigation arbitration. Existing input, divider and shutdown components remain owners of their original operations.
- `README.md`: keys, selection lifetime, client clipboard destination, 64 KiB bound, deterministic tmux framing, terminal/outer-layer prerequisites and unsupported/unverified outcomes. A bounded console-border title supplies visible key/status guidance without logging selection data. No application configuration options/defaults were introduced.

## Build and static verification

- Isolated build root: `/home/vollate/.cache/obcx/tui-selection-20261102/`.
- Configured core-only against the previously frozen workspace with explicit empty actor roots; package state/output/install prefix are isolated from the live core workspace and installed actor SDK.
- `cmake --build .../core --target obcx --parallel 20`: initial 216-step core/app build passed (`build.exit = 0`). Subsequent source/glyph/ANSI/input refinements passed the 14-step rebuild (`final-build.exit = 0`) and final 5-step source-mapping rebuild (`source-mapping-build.exit = 0`). A subsequent 20-worker freshness build reported `ninja: no work to do`. The final source-only hit-map refinement also built successfully (`selection-metadata-build.exit = 0`). Products: `core/src/libobcx_core.so`, `core/src/app/obcx`. No actor targets were built or products deployed.
- Initial build emitted a GCC/glibc fortified-memcpy warning through Boost.Asio resolver code in `src/network/websocket_client.cpp`; no TUI compilation warning/error was reported. Logs are retained under the isolated build root.
- Affected C++ sources were formatted with the repository `.clang-format`; `clang-format --dry-run --Werror` passed. `openspec validate tui-selection-copy-and-ctrl-d-exit --strict --json` passed (one change, zero issues), and both `git diff --check` and `git diff --cached --check` passed. Clipboard subprocess/thread-directed-signal/restored-I/O mechanism scans found none in the TUI sources. The sole writer call site is the explicit root Ctrl+C branch.
- Source inspection establishes implementation paths, not real signal/terminal/clipboard acceptance.

## FTXUI 7 build compatibility follow-up

- The operator's updated environment uses GCC 16.2.0 and FTXUI 7.0.3, where `ScreenInteractive` is an alias of `App`, not a class declaration. Removed the conflicting class forward declaration from `include/tui/tui_selection.hpp` and included the official `screen_interactive.hpp` instead. No standard-library changes or version-specific conditional aliases were needed.
- A fresh core-only build using the current Nix workspace launcher, explicit empty actor roots, GCC 16.2.0 and FTXUI 7.0.3 passed all 216 steps with 20 workers (`/tmp/obcx-ftxui7-20261102/build.exit = 0`). All three TUI sources, `libobcx_core.so` and the app compiled/linked successfully; the reported alias conflict and cascaded standard-library errors did not recur. Logs and products are under `/tmp/obcx-ftxui7-20261102/`; the live `build/` was not rebuilt or replaced by this verification.
- Formatting, diff and strict OpenSpec checks passed. This is build compatibility evidence, not FTXUI 7 interactive/clipboard acceptance; tasks 5.3/5.4 remain pending.

## Pending manual acceptance (tasks 5.3 and 5.4)

No operator-approved interactive acceptance environment/client clipboard was supplied. The operator explicitly chose to deliver the implementation first and retain these two manual acceptance tasks for operator verification. They remain unchecked; the change must not be treated as fully accepted or archived on build evidence alone.

| Environment | Observed outcome | Remaining evidence |
| --- | --- | --- |
| Direct interactive terminal | Not executed | Key priority and actual client paste, including Unicode, wraps and byte-limit rejection |
| SSH | Not executed | Client terminal policy allows OSC 52; non-sensitive selected text actually pastes on client |
| Active tmux | Not executed | Existing operator-owned passthrough/clipboard policies allow the one-envelope route |
| Nested SSH/tmux | Not executed; not automatically discovered or guaranteed | Every applicable layer permits forwarding and final client paste matches source |

Operator checklist using an approved, non-sensitive existing environment:

1. Select output with a nonempty command buffer; Ctrl+C preserves input and emits only an explicit copy request. With no selection it clears input/cursor and never exits, regardless of focused pane.
2. Ctrl+D with text or whitespace leaves input and process unchanged. On exactly empty input, verify actual process SIGINT reaches `SignalMonitor` and existing admission closure/drain/final-log shutdown runs; repeated key presses do not duplicate requests. Verify external SIGINT/SIGTERM and explicit `exit` separately. Non-TUI Ctrl+C/EOF must retain their old behavior.
3. Select across soft wraps, explicit/blank newlines, spaces/tabs, wide characters and combining suffixes. Paste must match the source substring without wrap-only newlines, controls or chrome. Mouse release/ordinary output must never copy.
4. While selected, allow new output, resize/reflow, and exercise retention eviction; source anchors remain stable or expire before copy. Escape clears without jumping to tail; End resumes log follow. Wheel/navigation, scrollbar drag and divider drag clear the selection first and keep their own controls.
5. Exercise empty, valid, exactly-at-limit and oversized selections; reject oversize atomically while retaining selection. Verify no OSC/DCS is emitted to noninteractive output and feedback never claims clipboard acknowledgment. Record each client/SSH/tmux/nested combination above, including declined or filtered requests.

Terminal policy changes and any deployment/restart/rollback remain operator-owned.
