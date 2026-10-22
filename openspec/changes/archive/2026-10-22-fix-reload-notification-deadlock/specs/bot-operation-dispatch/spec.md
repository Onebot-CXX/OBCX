## ADDED Requirements

### Requirement: Success observations use an explicit process-owned executor

When a process success handler is installed, `BotOperationDispatcher` SHALL require an explicit process-owned executor together with that handler. Successful endpoint results SHALL schedule observation asynchronously on that executor without awaiting the observation or inheriting the operation caller's executor. Failed endpoint results MUST NOT schedule success observation. Notification execution SHALL own the completed-operation value and keep its callable alive through callback completion. Observation scheduling or handler failure MUST NOT alter the completed operation result or cause the operation to be resent.

#### Scenario: Actor-owned executor submits a successful operation
- **WHEN** an operation invoked from a generation-owned executor succeeds while success observation is enabled
- **THEN** its notification runs on the explicitly registered process executor and retains no caller-executor work merely to deliver that notification
- **AND** the original caller can receive the successful operation result before observation finishes

#### Scenario: Provider operation fails
- **WHEN** an endpoint returns a failed operation result
- **THEN** the dispatcher returns that result without scheduling a success notification

#### Scenario: Originating operation coroutine has finished
- **WHEN** a success notification suspends after the originating invocation has returned and destroyed its local handler copy
- **THEN** the notification retains a valid callable and completed-operation value until its own terminal completion

#### Scenario: Observation cannot complete successfully
- **WHEN** posting a success notification fails or the observer throws
- **THEN** the completed provider result remains unchanged and no second provider invocation is performed to compensate for observation failure

### Requirement: Notification admission closes before process executor retirement

The dispatcher SHALL expose an idempotent boundary that closes new success-notification admission independently of operation endpoint closure. Acceptance and enqueue SHALL be synchronized with that boundary so a provider completion cannot submit work after notification shutdown has passed admission closure. Closing admission MUST NOT discard already accepted notification work. Process teardown SHALL release reload-gate waits and keep the notification executor and required dependencies alive while accepted callbacks finish or follow existing cancellation semantics, then drain the executor before stopping its Bot dependencies. Handler execution and executor joins MUST NOT occur while holding dispatcher admission locks, and the executor MUST NOT join itself.

#### Scenario: Provider completion races with admission closure
- **WHEN** a successful provider completion and notification-admission closure occur concurrently
- **THEN** either its notification is enqueued before closure and included in executor drain, or observation is skipped without changing the successful operation result
- **AND** no delayed handler snapshot can enqueue onto the executor after the closure boundary

#### Scenario: Accepted notification is waiting at ingress during shutdown
- **WHEN** process shutdown starts with an accepted notification waiting at the reload gate
- **THEN** notification admission closes and controller shutdown wakes the waiter while the notification executor remains running
- **AND** notification drain finishes before its required process-owned dependencies are stopped

#### Scenario: Provider completes after notification admission closes
- **WHEN** an already invoked provider operation succeeds after notification admission has closed
- **THEN** its caller receives the original success result without scheduling a new notification

#### Scenario: Early application exit follows observer registration
- **WHEN** startup failure or exception unwinding exits the application after success observation has been registered
- **THEN** cleanup applies the same admission-close, gate-release, callback-drain, and dependency-retirement ordering as normal shutdown

#### Scenario: Shutdown cleanup is repeated
- **WHEN** explicit shutdown and owner destruction both request notification cleanup
- **THEN** cleanup is idempotent and no notification can be admitted after closure or cause its worker to join itself
