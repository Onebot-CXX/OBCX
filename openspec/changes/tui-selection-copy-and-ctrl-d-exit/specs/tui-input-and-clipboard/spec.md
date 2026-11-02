## ADDED Requirements

### Requirement: Ctrl+C is selection-aware and never requests TUI shutdown
The fullscreen TUI SHALL handle Ctrl+C at the root input boundary before child
editor/default-exit behavior. An active valid text selection SHALL be copied
without clearing command input or requesting shutdown. Without a selection,
Ctrl+C SHALL clear command input/editor position only. Keyboard Ctrl+C MUST NOT
invoke exit, emit SIGINT or close the screen. This change SHALL NOT redefine
non-TUI terminal behavior or externally delivered signals.

#### Scenario: Selected output is copied while a command is being edited
- **WHEN** output text is selected, command input is nonempty and Ctrl+C is pressed
- **THEN** the selected text is submitted to the clipboard writer and command input remains unchanged
- **AND** no shutdown request is made

#### Scenario: No selection exists
- **WHEN** Ctrl+C is pressed without an active selection
- **THEN** command input is cleared without exiting or sending a clipboard write

#### Scenario: Focus is outside the command editor
- **WHEN** Ctrl+C is pressed while a different TUI pane has focus
- **THEN** the same copy-or-clear decision applies instead of falling through to a forced exit handler

### Requirement: Empty-input Ctrl+D requests process-directed graceful interruption
The fullscreen TUI SHALL request a process-directed SIGINT when Ctrl+D is pressed
and command input is exactly empty. The existing signal-monitor and graceful
shutdown path SHALL own admission closure, drain and TUI completion. Nonempty
input, including whitespace, SHALL cause Ctrl+D to be consumed without shutdown,
command execution or input deletion. A failed signal request SHALL be reported
without claiming shutdown began. Repeated keyboard requests after successful
submission SHALL NOT initiate multiple shutdown workflows.

#### Scenario: Empty input requests exit
- **WHEN** Ctrl+D is pressed with an empty command buffer in any pane focus
- **THEN** process-directed SIGINT reaches the existing signal monitor and normal graceful shutdown occurs
- **AND** TUI teardown does not bypass admitted-work drain

#### Scenario: Input contains text or whitespace
- **WHEN** Ctrl+D is pressed with a nonempty command buffer
- **THEN** the TUI remains active and neither executes nor discards that buffer

#### Scenario: A signal arrives independently of keyboard handling
- **WHEN** the process receives external SIGINT or SIGTERM
- **THEN** its existing graceful shutdown semantics remain in force

#### Scenario: Keyboard interruption is requested repeatedly
- **WHEN** Ctrl+D is pressed again after successful interrupt submission
- **THEN** no duplicate keyboard-owned shutdown workflow is started

### Requirement: Mouse selection copies source text rather than screen decoration
The TUI SHALL support visible mouse text selection in log and CLI output while
retaining existing command editing, scrollbar and divider controls. A selection
SHALL remain within its starting text pane and target the same retained source
text across ordinary refreshes while output collection continues. Ctrl+C extraction SHALL preserve
selected UTF-8 glyphs, spaces and explicit source newlines, omit soft-wrap-only
breaks, and exclude borders, scrollbar cells and terminal styling/control
sequences. Selection changes and mouse release SHALL NOT themselves copy text.

#### Scenario: A long entry wraps visually
- **WHEN** a selection spans soft-wrapped rows of one source line
- **THEN** its copied text contains the original selected substring without artificial wrap newlines

#### Scenario: Text contains explicit newlines and wide or combining glyphs
- **WHEN** those characters are selected
- **THEN** explicit line breaks and complete glyphs survive without byte splitting or duplicated text

#### Scenario: Drag crosses a pane boundary
- **WHEN** a selection drag leaves its starting text pane
- **THEN** selection is clamped to that pane and no border or other-pane text is appended

#### Scenario: Mouse drag ends
- **WHEN** the user releases the selection drag without pressing Ctrl+C
- **THEN** the selection remains available without emitting a clipboard write

#### Scenario: User cancels a selection
- **WHEN** Escape is pressed while a selection is active
- **THEN** that selection is cleared without copying or requesting shutdown

### Requirement: Clipboard writes target the client terminal over SSH and tmux
An explicit selection copy SHALL emit a write-only OSC 52 clipboard request with
Base64-encoded plain UTF-8 text and clipboard selector c to the interactive
terminal stream. Running over SSH SHALL not require a graphical clipboard on
the server. Active-tmux framing SHALL use correctly escaped tmux passthrough;
applicable outer/nested multiplexers and the final terminal MUST permit
forwarding. The application SHALL document these prerequisites without claiming
automatic discovery of every nested layer or bypassing denied terminal policy.
It MUST NOT execute xclip, wl-copy, tmux configuration commands or remote shell
commands to implement the copy.

#### Scenario: Remote TUI is reached through SSH
- **WHEN** Ctrl+C copies a selection and the client terminal permits OSC 52
- **THEN** the terminal clipboard-write request travels back through SSH to the client machine, not a server desktop clipboard

#### Scenario: TUI runs inside tmux
- **WHEN** active tmux requires passthrough and it is permitted
- **THEN** correctly escaped framing carries the same selected bytes through tmux without changing its configuration

#### Scenario: SSH and tmux are nested
- **WHEN** every applicable multiplexer layer and the final terminal allow the documented forwarding route
- **THEN** the copy can reach the client clipboard without an application-side clipboard daemon or SSH helper
- **AND** missing permissions are documented limitations, not a claimed successful clipboard mutation

### Requirement: Clipboard output is bounded and serialized with TUI rendering
A single copy SHALL have a documented 64 KiB raw UTF-8 byte safety ceiling,
validated before encoding/output. This implementation bound SHALL NOT introduce
a new application configuration default or imply every terminal accepts that
size. Oversized selections MUST be rejected atomically without
partial, truncated or chunked clipboard replacement. Clipboard framing/output
SHALL execute through one serialized TUI-owned output path without interleaving
frames or entering the log sink. The writer SHALL NOT emit terminal control
sequences to redirected/noninteractive output.

#### Scenario: Selection exceeds the supported copy ceiling
- **WHEN** selected UTF-8 text is larger than the documented byte ceiling
- **THEN** no clipboard sequence is written, the selection remains available and bounded feedback identifies the limit

#### Scenario: Rendering and copying coincide
- **WHEN** a copy is requested while log refreshes are queued
- **THEN** the complete clipboard sequence is serialized between rendering operations rather than interleaved with a frame

#### Scenario: Output is not an interactive terminal
- **WHEN** clipboard output would be sent to redirected/noninteractive output
- **THEN** the write is refused without emitting OSC/DCS into that stream

### Requirement: Copy feedback does not expose contents or assert an acknowledgement
The application SHALL distinguish local validation/output failure from a copy
request being emitted. It MUST NOT claim verified remote clipboard success
without evidence, query/read clipboard contents, or log selected text/Base64.
New log output, selection changes and keyboard input other than explicit copy
MUST NOT cause clipboard writes. Unsupported clipboard transport SHALL NOT
prevent normal TUI use or graceful shutdown.

#### Scenario: Terminal silently declines the clipboard request
- **WHEN** an OSC 52 sequence is emitted but the terminal denies or ignores it
- **THEN** application feedback states only that the request was emitted and does not assert the client clipboard changed

#### Scenario: Copy diagnostics are inspected
- **WHEN** copying succeeds locally or fails validation/output
- **THEN** diagnostics contain only bounded status/size information and no selected content or clipboard reads

#### Scenario: Ordinary output continues
- **WHEN** logs arrive or the highlighted range changes without Ctrl+C
- **THEN** no OSC 52 clipboard write is emitted
