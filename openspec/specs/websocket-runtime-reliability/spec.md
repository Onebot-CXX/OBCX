# websocket-runtime-reliability Specification

## Purpose
Define deterministic, bounded WebSocket write and OneBot action-completion reliability for the root runtime.

## Requirements
### Requirement: Concurrent WebSocket writes are serialized and bounded
`WebsocketClient` SHALL serialize admitted writes through one bounded FIFO writer so that no two Beast write operations overlap. A caller whose request is admitted SHALL complete only after its own write reaches a terminal success or failure, and queue saturation SHALL apply asynchronous backpressure rather than dropping or reordering an accepted message.

#### Scenario: Several callers send concurrently
- **WHEN** the first transport write is held and several additional sends are admitted concurrently
- **THEN** exactly one transport write is active, subsequent writes start in admitted FIFO order, and every caller completes exactly once with the outcome of its own write

#### Scenario: The bounded queue is saturated
- **WHEN** admitted and queued writes reach the configured capacity
- **THEN** an additional sender remains asynchronously backpressured until capacity becomes available or the writer terminates, without busy polling or message loss

### Requirement: Writer termination retires every admitted sender
A write failure, connection closure, or client shutdown SHALL deliver one terminal result to the active write and every admitted waiter that cannot still be written. No write-completion state or suspended sender MAY outlive terminal writer cleanup, and failure diagnostics MUST NOT contain access tokens or complete message payloads.

#### Scenario: Active write fails
- **WHEN** the transport reports a failure for the active write while more requests are queued
- **THEN** the active caller receives that failure and every request that cannot proceed is failed or cancelled exactly once without starting overlapping writes

#### Scenario: Client closes under backpressure
- **WHEN** shutdown occurs with one active write, queued requests, and a sender waiting for queue capacity
- **THEN** all admitted and backpressured senders reach bounded terminal cancellation and the queue retains no waiters

### Requirement: OneBot action completion is correlated and exactly once
`WebSocketConnectionManager` SHALL correlate each pending action by echo identity and complete it through one terminal state transition: response, timeout, transport failure, or cancellation. The winning transition SHALL remove the pending entry and disarm losing completion sources so a response/deadline/close race cannot resume a caller twice.

#### Scenario: Matching response arrives first
- **WHEN** a valid response with the pending echo arrives before its deadline
- **THEN** the caller receives that response once, the deadline is disarmed, and the pending entry is removed

#### Scenario: Deadline expires first
- **WHEN** the pending action deadline fires before a matching response
- **THEN** the caller receives the stable timeout failure once, the pending entry is removed, and a later matching response cannot complete the old caller

#### Scenario: Response and timeout race
- **WHEN** response delivery and deadline expiry are forced in either order at the terminal boundary
- **THEN** exactly one transition wins, the caller completes once, and no pending entry or deadline remains

#### Scenario: Connection closes with pending actions
- **WHEN** the connection manager disconnects or is destroyed while actions are pending
- **THEN** every pending caller receives bounded cancellation or transport failure and the pending-action table becomes empty

### Requirement: WebSocket reliability verification is deterministic
The normal root CI suite SHALL verify write serialization, bounded backpressure, completion, timeout, race, and cleanup using controllable write and deadline events. Fixed sleeps, elapsed-time windows, or weak-network delay simulation MUST NOT be the correctness condition; real time MAY be used only as a bounded watchdog that fails a deadlocked test.

#### Scenario: Write contention is tested
- **WHEN** the deterministic queue test runs
- **THEN** a manual transport gate proves the active-write count, FIFO start order, backpressure state, and per-caller completion without waiting for network timing

#### Scenario: Timeout boundaries are tested
- **WHEN** response-before-timeout and timeout-before-response cases run
- **THEN** a manual deadline source selects the event order explicitly and the tests complete without waiting for the production timeout duration

#### Scenario: CI inventory is inspected
- **WHEN** normal CI configures root tests without experimental network options
- **THEN** the deterministic WebSocket reliability cases are registered and executed

### Requirement: Beast integration has bounded smoke coverage
The root suite SHALL retain bounded integration coverage that connects the production Beast client to a loopback server and verifies handshake and one write/read flow. Server readiness, connection establishment, and completion SHALL use explicit asynchronous signals rather than fixed startup sleeps.

#### Scenario: Loopback WebSocket smoke runs
- **WHEN** the loopback server reports that it is listening and the production client connects
- **THEN** one bounded message flow completes successfully and teardown joins both sides without timing-based readiness assumptions
