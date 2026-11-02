## MODIFIED Requirements

### Requirement: Visual-row log navigation
The TUI SHALL calculate the log viewport, scrolling limits, page movement,
tail-follow behavior, and scrollbar from wrapped visual rows rather than stored
logical-entry count. Beginning text selection SHALL pin the selected source
content and suspend automatic tail-follow for that pane without stopping output
collection. Reflow/resize SHALL preserve selected source identity rather than
reusing old screen coordinates. If exact selected anchors become unavailable,
the TUI SHALL clear the selection before copying and provide bounded feedback;
it MUST NOT copy replacement content from the old cells. Navigation that cannot
retain exact selection SHALL clear it before moving. Clearing a selection SHALL
NOT implicitly jump to the tail.

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

#### Scenario: Logs arrive during text selection
- **WHEN** a log selection is active and new entries arrive
- **THEN** collection continues while the selected content and inspected viewport remain pinned
- **AND** Ctrl+C copies the selected original text rather than newer rows at the same coordinates

#### Scenario: Selection survives a layout reflow
- **WHEN** terminal width or pane geometry changes while selected source anchors remain retained
- **THEN** highlighting is recomputed from source identity and the copied text remains unchanged

#### Scenario: Selected source is evicted
- **WHEN** bounded retention removes text required by an active selection
- **THEN** selection is invalidated with bounded feedback before it can copy stale or replacement content
- **AND** log retention is not extended to preserve an unbounded selection snapshot

#### Scenario: Navigation abandons a selection
- **WHEN** paging, scrollbar movement or another viewport action cannot preserve the selected source range
- **THEN** selection is cleared before navigation rather than silently retargeted

### Requirement: Interactive pane resizing
The TUI SHALL display a distinct horizontal divider between the log and CLI
panes and SHALL allow the user to resize the panes with either mouse dragging
or keyboard controls. Ctrl+Up SHALL move the divider up by one row and
Ctrl+Down SHALL move it down by one row, subject to pane minimums. Divider and
scrollbar hit regions SHALL retain their interaction priority over text
selection. A control drag SHALL NOT start a text selection or clipboard write;
a text-selection drag SHALL NOT become a pane resize.

#### Scenario: User drags the divider
- **WHEN** the user presses the divider, drags it vertically, and releases it within the allowed layout range
- **THEN** the divider and both pane heights follow the selected position and that position becomes the preferred split

#### Scenario: User resizes with the keyboard
- **WHEN** the TUI receives Ctrl+Up or Ctrl+Down
- **THEN** it moves the divider one valid row in the requested direction and updates the preferred split

#### Scenario: User resizes beyond a pane minimum
- **WHEN** mouse or keyboard input requests a divider position that would make a pane smaller than its usable minimum
- **THEN** the TUI clamps the divider at that minimum without producing invalid rendering or scroll state

#### Scenario: Output is visible during pane resizing
- **WHEN** the divider changes either pane's height
- **THEN** both panes, the log viewport, and scrollbar geometry update on the next rendered frame without restarting the TUI

#### Scenario: Scrollbar or divider receives a mouse press
- **WHEN** a left-button press begins within an existing scrollbar or divider control
- **THEN** that control owns its drag without creating a text selection or triggering clipboard output

#### Scenario: Text is selected near a control
- **WHEN** a text selection begins in content and is dragged across a control boundary
- **THEN** it remains a selection gesture clamped to its starting pane rather than moving the divider or scrollbar
