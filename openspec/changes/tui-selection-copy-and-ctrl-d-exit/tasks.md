## 1. Confirm existing TUI integration seams

- [x] 1.1 Verify FTXUI 6.1.9 root key dispatch, Ctrl+C terminal/default handling, GetSelection/SelectionChange/highlighting and mouse-capture behavior against the existing panes without adding a validation program.
- [x] 1.2 Identify exact log/console source-to-cell mapping and process-directed interrupt wiring; record how existing entry retention, soft wraps, explicit newlines and SignalMonitor ownership are preserved.

## 2. Implement source-backed text selection

- [x] 2.1 Add the minimal selection state/mapping adapter and reuse FTXUI highlighting for contiguous log/CLI output selections with complete UTF-8 glyphs and starting-pane clamping.
- [x] 2.2 Extract selected plain source text without borders, scrollbar/style/control sequences or artificial soft-wrap newlines; preserve explicit newlines/spaces and existing input editing.
- [x] 2.3 Pin the selected pane's source/view while output collection continues; preserve anchors across reflow/resize and explicitly invalidate unavailable/evicted ranges before copy.
- [x] 2.4 Preserve divider/scrollbar drag priority, clear selection before incompatible navigation, support Escape cancellation and ensure no selection/drag event itself writes the clipboard.

## 3. Implement bounded terminal clipboard delivery

- [x] 3.1 Add bounded Base64 OSC 52 clipboard-write encoding with a 64 KiB raw UTF-8 safety ceiling, validating/rejecting the whole selection before output without truncation or clipboard queries.
- [x] 3.2 Add deterministic active-tmux DCS framing and plain non-tmux output; do not spawn clipboard/SSH/tmux subprocesses or change external configuration.
- [x] 3.3 Serialize one complete clipboard write/flush through the interactive TUI output owner between frames; reject noninteractive output and keep escapes out of logging/rendered text.
- [x] 3.4 Add bounded content-safe emitted/unavailable/too-large feedback without logging text/Base64, clearing rejected selections or claiming verified remote clipboard success.

## 4. Wire the selected keyboard behavior

- [x] 4.1 Handle Ctrl+C at the root: copy active selection without editing input; otherwise clear command input/editor position; prevent FTXUI/child fallback exit behavior.
- [x] 4.2 Handle Ctrl+D at the root: consume nonempty/whitespace input unchanged; for exactly empty input request process-directed SIGINT through the existing process signal owner, with duplicate/failure handling and no direct screen/process exit.
- [x] 4.3 Preserve external SIGINT/SIGTERM, explicit exit commands, non-TUI Ctrl+C/EOF, existing shutdown drain and final TUI output behavior.

## 5. Document, build and record acceptance boundaries

- [x] 5.1 Update core TUI key/copy documentation and visible guidance, including selection lifecycle, the byte ceiling, client-versus-server clipboard, terminal OSC 52 policy, tmux passthrough/outer-layer prerequisites and nested SSH/tmux limitations; introduce no application config defaults.
- [x] 5.2 Format affected sources and build core/app in an isolated directory using at least six workers, or all available cores when load permits; do not deploy, restart or build actor targets in the live core workspace.
- [ ] 5.3 Check key priority, empty/nonempty Ctrl+D, real graceful SIGINT, Unicode/wrap/source extraction, live-output anchoring, resize/eviction/navigation and control-drag separation with existing authorized manual facilities and non-sensitive text; add no unit tests or new validation programs.
- [ ] 5.4 Record direct-terminal, SSH, tmux and nested SSH/tmux clipboard outcomes using operator-approved environments; explicitly list unsupported/unexecuted combinations and never infer clipboard/runtime success from compilation or local byte emission.
- [x] 5.5 Run strict OpenSpec and diff checks; record code/build/manual acceptance separately and leave terminal settings, deployment/restart and any rollback to the operator.
