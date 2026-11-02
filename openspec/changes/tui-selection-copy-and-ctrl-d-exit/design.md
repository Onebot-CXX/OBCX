## Context

The user selected three behaviors: client-machine clipboard delivery over SSH/
tmux, explicit Ctrl+C copy (otherwise clear input), and Ctrl+D SIGINT only with
an empty command buffer. This is a core TUI change, not an actor/SDK migration.

`src/tui/tui_app.cpp` uses FTXUI Fullscreen, disables FTXUI's forced Ctrl+C
handling and currently handles Ctrl+C inside the command-input wrapper by
requesting exit. It refreshes every 100 ms. Logs are virtualized through
WrappedLogCache/LogViewport with sequence-based anchors; console history is a
separately rendered vector. Existing mouse handling owns wheel, scrollbar,
input-focus and divider interactions. FTXUI 6.1.9 supplies GetSelection,
SelectionChange and selection styling, but these APIs alone do not establish
logical-source copying or stability across a re-render.

`src/app/main.cpp` blocks SIGINT/SIGTERM before starting worker threads and uses
a dedicated SignalMonitor thread with sigtimedwait. A thread-directed raise can
leave SIGINT pending on the blocked calling thread; copying the current stop-flag
assignment would not satisfy the requested signal semantics.

## Goals / Non-Goals

**Goals:**

- Make the selected keyboard behavior independent of which TUI pane has focus.
- Reuse actual process SIGINT and existing stop/admission/drain sequencing.
- Select log/CLI output with visible highlighting and source-correct copying.
- Deliver bounded clipboard writes through SSH/tmux to the client terminal.
- Keep scrolling, resizing, log retention, UTF-8 and rendering ownership safe.
- Document prerequisites and honest copy outcomes without new config defaults.

**Non-Goals:**

- No implementation, deployment or commit as part of proposal creation.
- No server desktop clipboard, clipboard reads/synchronization daemon, shell
  clipboard commands, remote command execution or tmux configuration mutation.
- No automatic copy on drag, release, log arrival or selection changes.
- No change to non-TUI Ctrl+C/EOF, explicit exit commands or external signals.
- No actor, SDK, database, provider, logging-retention or shutdown-policy redesign.
- No new unit tests/validation programs or configurable clipboard options.
- No unbounded scrollback selection, rectangle/word/line/vi copy mode, cross-pane
  concatenation or guaranteed interoperability with every terminal policy.

## Decisions

### 1. Route keyboard intent at the root and reuse process-owned interruption

Handle Ctrl+C/Ctrl+D before child components can apply editor/exit behavior.
With an active selection, Ctrl+C copies that selection and leaves command input
unchanged. Without a selection it clears command text/editor position only.
Ctrl+D consumes the key without exit when input is nonempty; it does not execute
or discard that input. With empty input, request process-directed SIGINT via
`kill(getpid(), SIGINT)` through a small process-owned callback/wiring seam.
Do not use raise/pthread_kill, directly call screen.Exit, terminate the process,
or substitute a stop-flag write. Suppress duplicate keyboard interrupt requests
after successful signal submission; surface send failure without pretending exit
began. Retain external SIGINT/SIGTERM and the existing graceful shutdown owner.

Verify FTXUI's actual Ctrl+C event/terminal behavior rather than relying solely
on a potentially version-dependent option comment. Keyboard Ctrl+C must arrive
as application input, while externally sent SIGINT must still reach SignalMonitor.

**Alternatives rejected:** directly changing one key constant leaves the signal
path and focus behavior wrong; retaining Ctrl+C exit conflicts with copy;
allowing Ctrl+D to exit with nonempty input violates the chosen safety behavior.

### 2. Reuse FTXUI selection, with a source-coordinate adapter where necessary

First verify FTXUI selection/highlighting with the existing virtualized DOM.
Keep one root event loop and reuse built-in mechanics wherever correct. Add a
small mapping/state layer for any guarantees the screen-coordinate selection
cannot supply: pane identity, retained log sequence or console-entry identity,
and glyph-safe offsets into the corresponding plain source text.

Selection is a contiguous range within its starting text pane. Dragging across
a border clamps to that pane rather than copying chrome or concatenating panes.
Scrollbar/divider drags retain their current priority and never start selection.
Escape clears active selection; otherwise it retains existing behavior. A new
selection replaces the old one. Selection state itself never writes a clipboard.

Copied text comes from source-backed visible text, not terminal frame dumps:
soft-wrap boundaries do not become newlines, explicit source newlines/spaces
survive, and partial wide/combining glyphs are not split. Preserve display mapping
when removing ANSI/control decoration; the current SGR-only strip_ansi helper is
not assumed to sanitize arbitrary OSC/terminal controls. Input-editor selection,
if exposed by FTXUI, must follow the same copy-first Ctrl+C priority; this change
does not replace normal command editing with a second editor.

### 3. Pin the selected content, not the data producer

Beginning output selection suspends automatic tail-follow for that pane and
pins logical source anchors. Logging and CLI output continue to be collected.
Reflow/resize recomputes highlighting from those anchors without changing the
selected bytes. Navigation clears the selection before moving the viewport if
it would otherwise retarget screen coordinates. Clearing selection does not
silently jump to the tail; existing End/navigation can restore tail-follow.

If retention eviction or layout invalidation makes exact anchors unavailable,
clear the selection with bounded feedback before it can be copied. Never
substitute whatever text now occupies the old screen cells. Do not extend log
retention or retain an unbounded second scrollback to keep a selection alive.

**Alternative rejected:** freezing ingestion would block useful runtime output;
coordinate-only copying can silently copy different lines after refresh.

### 4. Emit bounded write-only OSC 52 on the TUI output owner

Encode selected UTF-8 text as Base64 in a clipboard-write OSC 52 sequence using
the clipboard selector `c`. Keep a non-configurable 64 KiB raw UTF-8 safety
ceiling for a single copy; validate the whole selection before output. Reject
larger selections atomically with feedback, never silently truncate/split them
into clipboard writes (later writes would replace earlier content). This is an
implementation resource bound, not an application configuration default or a
claim about every terminal's supported maximum.

The transport writes one complete sequence on the TUI loop/output owner between
frames, with a serialized flush. It must not interleave with rendering, invoke
WithRestoredIO merely to copy, put escapes into log/CLI text, or emit control
sequences to redirected/noninteractive output. Never log selected text or its
Base64 representation and never issue an OSC 52 query/read.

For an active tmux indicated by nonempty TMUX, encode one correctly escaped DCS
tmux passthrough envelope around the OSC write. Without it, emit plain OSC 52.
SSH carries the terminal bytes; no SSH-specific command is needed. A client-side
or outer tmux not visible to the remote process must allow OSC 52 forwarding.
Document current tmux `allow-passthrough` requirements for DCS and `set-clipboard`
/terminal clipboard capability requirements for applicable outer layers. All
multiplexer layers and the final terminal must permit the operation. Environment
presence is a framing hint, not proof of capability or the number of nested layers.
Do not send both plain and wrapped writes speculatively or change tmux settings.

**Alternatives rejected:** xclip/wl-copy would target the server's display;
subprocess clipboard pipelines add dependencies and quoting risks; pretending
OSC 52 has a portable acknowledgement would give false success reports.

### 5. Report local outcomes without claiming remote success

Use bounded UI status such as copy request emitted, no selection, copy too large,
or clipboard output unavailable. A successful write means only that bytes were
emitted; OSC 52 provides no reliable portable acknowledgement of clipboard
mutation. Keep selection available after a rejected copy. Do not query private
clipboard state to confirm delivery. Terminal-native selection remains an
operator fallback when OSC 52 is unsupported or disabled.

## Risks / Trade-offs

- **FTXUI selection may be screen-oriented** → Verify actual behavior first; add
  only the necessary source mapping and preserve one renderer/event owner.
- **Growing output/reflow changes cells** → Logical anchors, pinned view and
  explicit invalidation prevent copying unrelated text.
- **Unicode/wrap ambiguity** → Use FTXUI-compatible glyph/cell mapping and retain
  explicit source breaks separately from soft wrapping.
- **Clipboard forwarding is terminal-controlled** → Document prerequisites and
  report emission, not guaranteed success. Nested tmux must be configured by its
  owner; no application can bypass a denied terminal policy reliably.
- **Large or malicious terminal data** → Bound before encoding, copy plain source
  text only, use fixed framing/Base64, and prohibit automatic writes/queries.
- **Signal targets the wrong thread or exits too early** → Process-directed
  delivery, root key interception and unchanged SignalMonitor/drain ownership.
- **User muscle memory changes** → Update visible key guidance and migration
  notes; non-TUI and external signal semantics remain unchanged.

## Migration Plan

1. Implement core-only input, selection and clipboard changes plus documentation.
2. Build core/app in a new isolated directory with at least six workers; use all
   available cores when system load permits. No actor rebuild or live-core build
   is required for this internal TUI change unless actual interface scope changes.
3. Perform authorized manual acceptance with non-sensitive text: direct terminal,
   SSH, tmux, nested SSH/tmux, Unicode/wrapping, live-output selection, invalidation,
   Ctrl+C priority and graceful SIGINT. Do not add unit tests or new validation
   programs. Record unsupported/unexecuted combinations honestly.
4. Leave deployment/restart, tmux/terminal settings and any rollback to the
   operator. There is no data migration. Rollback restores the preceding core
   binary and its keyboard behavior, not changes to actor/database state.

## Open Questions

No product-level decision is outstanding. The user's key/copy/destination choices
are fixed. FTXUI event/selection details and concrete terminal/tmux capability
limits are implementation/acceptance checks, not grounds to silently change the
contract. Actual interoperability remains unverified at proposal time.
