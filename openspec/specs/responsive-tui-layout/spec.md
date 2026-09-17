# responsive-tui-layout Specification

## Purpose
Define width-aware output wrapping, visual-row navigation, and responsive interactive pane sizing for the terminal UI.

## Requirements
### Requirement: Width-aware TUI output wrapping
The TUI SHALL wrap log entries and command-history output to the current content width in terminal cells, preserving all visible text, explicit line breaks, and source log-level styling. The TUI MUST NOT split a UTF-8 glyph and SHALL hard-wrap text that has no usable whitespace boundary.

#### Scenario: Log text exceeds the pane width
- **WHEN** a log entry requires more terminal cells than the log content area provides
- **THEN** the TUI displays the complete entry on consecutive visual rows within the log pane

#### Scenario: A long token has no wrapping boundary
- **WHEN** a URL, identifier, or other unbroken token is wider than the content area
- **THEN** the TUI hard-wraps the token at glyph boundaries without clipping, duplicating, or dropping visible text

#### Scenario: Output contains wide or combining glyphs
- **WHEN** output contains UTF-8 glyphs whose terminal width is not one cell
- **THEN** wrapping uses FTXUI-compatible terminal-cell widths and keeps each glyph intact

#### Scenario: A pane width changes
- **WHEN** the terminal width changes while output is displayed
- **THEN** the TUI reflows the output to the new content width on the next rendered frame

### Requirement: Visual-row log navigation
The TUI SHALL calculate the log viewport, scrolling limits, page movement, tail-follow behavior, and scrollbar from wrapped visual rows rather than stored logical-entry count.

#### Scenario: New wrapped output arrives while following the tail
- **WHEN** the log viewport is following the tail and a new entry adds one or more visual rows
- **THEN** the newest visual rows remain visible and the bottom offset remains zero

#### Scenario: New output arrives while inspecting history
- **WHEN** the user has scrolled above the tail and new wrapped output arrives
- **THEN** the TUI preserves the currently inspected content instead of jumping to the new tail

#### Scenario: The log pane is reflowed while scrolled
- **WHEN** a width change alters the number of visual rows while the user is inspecting history
- **THEN** the TUI restores the nearest visual row belonging to the same retained logical log entry and clamps only if that entry is no longer retained

#### Scenario: Scrollbar represents wrapped content
- **WHEN** one or more logical log entries occupy multiple visual rows
- **THEN** the scrollbar range and thumb size represent the total wrapped visual-row count and the current visual-row viewport

### Requirement: Responsive log and CLI split
The TUI SHALL initialize the log and CLI panes before their first render with an approximately 70/30 vertical allocation, and SHALL preserve the latest user-selected pane proportion when terminal height changes. Pane dimensions MUST be clamped to valid values and SHALL leave at least one content row in each bordered pane whenever the terminal is large enough.

#### Scenario: TUI renders for the first time
- **WHEN** the TUI starts in a terminal large enough for both panes
- **THEN** the first rendered frame allocates approximately 70 percent of available pane rows to logs and 30 percent to the CLI

#### Scenario: Terminal height changes before manual resizing
- **WHEN** the terminal height changes before the user moves the divider
- **THEN** the TUI recomputes integer pane heights using the initial proportion and the new available height

#### Scenario: Terminal height changes after manual resizing
- **WHEN** the user selects a different split and the terminal height subsequently changes
- **THEN** the TUI recomputes pane heights using the user's latest selected proportion

#### Scenario: Terminal is unusually short
- **WHEN** the terminal cannot satisfy both normal pane minimums
- **THEN** the TUI produces non-negative in-bounds geometry and restores both usable content areas when sufficient height becomes available

### Requirement: Interactive pane resizing
The TUI SHALL display a distinct horizontal divider between the log and CLI panes and SHALL allow the user to resize the panes with either mouse dragging or keyboard controls. `Ctrl+Up` SHALL move the divider up by one row and `Ctrl+Down` SHALL move it down by one row, subject to pane minimums.

#### Scenario: User drags the divider
- **WHEN** the user presses the divider, drags it vertically, and releases it within the allowed layout range
- **THEN** the divider and both pane heights follow the selected position and that position becomes the preferred split

#### Scenario: User resizes with the keyboard
- **WHEN** the TUI receives `Ctrl+Up` or `Ctrl+Down`
- **THEN** it moves the divider one valid row in the requested direction and updates the preferred split

#### Scenario: User resizes beyond a pane minimum
- **WHEN** mouse or keyboard input requests a divider position that would make a pane smaller than its usable minimum
- **THEN** the TUI clamps the divider at that minimum without producing invalid rendering or scroll state

#### Scenario: Output is visible during pane resizing
- **WHEN** the divider changes either pane's height
- **THEN** both panes, the log viewport, and scrollbar geometry update on the next rendered frame without restarting the TUI
