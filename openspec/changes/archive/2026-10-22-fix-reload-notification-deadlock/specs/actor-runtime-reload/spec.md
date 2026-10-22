## ADDED Requirements

### Requirement: Success notification ingress is independent of retiring executors

Successful-operation notifications that become new actor root events SHALL enter through the existing reload ingress gate using a process-owned notification executor. Actor reload MUST NOT stop or join that executor. A notification waiting outside the closed gate MUST NOT retain the originating generation's executor work or acquire a lease on that generation merely to wait for reopening. A notification admitted before gate closure SHALL retain normal route-drain and generation-scoped execution semantics. This separation MUST preserve retirement-before-reopen ordering, existing drain-timeout behavior, and safe actor library lifetimes; it MUST NOT enable Bot configuration changes during reload.

#### Scenario: Send completion creates notification while ingress is closed
- **WHEN** an old-generation operation succeeds during cutover and its notification reaches the closed ingress gate before obtaining route admission
- **THEN** the notification waits on the process executor without preventing the originating operation and old actor I/O executor from retiring
- **AND** after successful cutover the notification enters the selected new generation exactly once

#### Scenario: Notification entered the old generation before closure
- **WHEN** a success notification acquired old-generation route admission before the reload gate closed
- **THEN** its routed work remains on the old generation and participates in the existing drain deadline rather than bypassing drain or migrating to the candidate

#### Scenario: Unrelated admitted work causes a drain timeout
- **WHEN** reload times out waiting for old-generation route leases while a success notification waits outside ingress
- **THEN** the old generation is retained and the reopened gate admits the waiting notification to that old generation without losing it solely because reload was attempted

#### Scenario: Reload follows a completed notification overlap
- **WHEN** the old generation retires, ingress reopens, and a later reload is requested after successful-operation notifications overlapped the previous cutover
- **THEN** the previous reload has released its busy reservation and the later request is not rejected as busy because a notification retained the retired executor

#### Scenario: Candidate changes Bot enabled state
- **WHEN** a candidate enables or disables a Bot installation while process notification handling is configured
- **THEN** preparation still rejects the candidate as restart-required before gate closure and leaves the current Bot installations and notification executor unchanged
