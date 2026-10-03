# database-manager-transactions Specification

## Purpose
Define atomic, writer-serialized transaction behavior for process-managed database connections and installed SDK consumers.

## Requirements
### Requirement: Database manager transactions are atomic and writer-serialized
`DbManager` SHALL expose a typed transaction operation whose callback executes through the selected connection provider's serialized writer path. A successful callback SHALL commit all of its database changes atomically. If a callback or commit fails after transaction start, the provider SHALL attempt rollback before propagating the failure. Transaction callbacks MUST be synchronous and MUST NOT hold a transaction across coroutine suspension or external network work.

#### Scenario: Transaction succeeds with a result
- **WHEN** a transaction callback performs multiple valid writes and returns a value
- **THEN** all writes commit together and the typed value is returned to the caller

#### Scenario: Callback fails after writes
- **WHEN** a transaction callback changes rows and then throws
- **THEN** none of that transaction's changes remain committed and the failure reaches the caller

#### Scenario: Concurrent writes target one connection
- **WHEN** ordinary write tasks and transaction tasks are submitted concurrently to one configured connection
- **THEN** the provider serializes them on its writer path so no other write interleaves inside the transaction

### Requirement: Transaction failure does not strand the connection
After a failed transaction, the provider SHALL leave the connection and writer usable for later reads, writes, transactions, and migrations. Rollback failure MUST NOT replace the original transaction failure with a false success.

#### Scenario: Write follows rolled-back transaction
- **WHEN** one transaction is rolled back because its callback fails and a later valid write is submitted
- **THEN** the later write completes normally and observes no partial rows from the failed transaction

#### Scenario: Migration uses transaction ownership
- **WHEN** namespace migration work runs under `DbManager` migration locking
- **THEN** migration lock updates and migration callback changes share the provider-owned atomic transaction boundary

### Requirement: Transaction support is part of the installed core SDK
The installed OBCX core SDK SHALL expose the typed `DbManager` transaction API and the provider transaction contract. Database providers and SDK consumers MUST build against the matching interface rather than bypassing `DbManager` with a second connection to a process-owned database.

#### Scenario: Clean SDK consumer uses a transaction
- **WHEN** a consumer is configured using only an installed matching OBCX SDK and its declared dependencies
- **THEN** it can compile and link a typed transaction call without access to the OBCX source tree
