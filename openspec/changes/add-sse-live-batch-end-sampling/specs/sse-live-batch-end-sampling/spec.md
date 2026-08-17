## ADDED Requirements

### Requirement: Strict SSE batch-end detection
The system SHALL close an active SSE market-data batch only after a local
receive-time gap strictly greater than 100000 nanoseconds and SHALL use a
trailing-edge one-shot deadline rather than fixed-period MD publication.

#### Scenario: Gap exceeds threshold
- **WHEN** an active batch last receives an update at `T` and the detector observes a timer or next event later than `T + 100000ns`
- **THEN** the system emits exactly one batch-end result for that batch

#### Scenario: Gap equals threshold
- **WHEN** the next update or timer observation occurs exactly `100000ns` after the last update
- **THEN** the system keeps the batch open and emits no result

#### Scenario: Update inside threshold
- **WHEN** another SSE update arrives less than or equal to `100000ns` after the last update
- **THEN** the system coalesces it into the same batch and rearms the one-shot deadline

### Requirement: Batch closes before the next batch mutates the book
The system SHALL detect a crossed batch deadline from the next event timestamp
before the caller applies that event to the reconstructed order book.

#### Scenario: Timer service is late
- **WHEN** a new event arrives with a receive gap greater than 100000ns before the expired timer callback is serviced
- **THEN** the system closes the previous batch before accepting the new event into a new batch

### Requirement: CompleteOrderBookSH candidate coalescing
The system SHALL emit only the latest eligible CompleteOrderBookSH candidate
per instrument in a batch and SHALL not create candidates from timer ticks or
unchanged books.

#### Scenario: Multiple candidates for one instrument
- **WHEN** a batch contains multiple increasing CompleteOrderBookSH cut indexes for one instrument
- **THEN** the emitted batch contains only that instrument's final candidate

#### Scenario: No eligible candidates
- **WHEN** a batch contains activity but no CompleteOrderBookSH candidate
- **THEN** batch close emits no model sample

#### Scenario: Duplicate candidate
- **WHEN** a candidate cut index is not greater than the last emitted cut index for its instrument
- **THEN** the system suppresses it and recurrent model state is not advanced

### Requirement: Fail-closed feed health
The system SHALL discard a pending batch on a sequence-health failure or
non-monotonic local receive timestamp and SHALL remain unable to emit until an
explicit recovery/reset.

#### Scenario: Sequence gap
- **WHEN** the decoder reports a sequence gap while a batch is active
- **THEN** the system discards all pending candidates and emits no sample from the incomplete batch

#### Scenario: Explicit recovery
- **WHEN** the decoder completes resynchronization and resets the sampler
- **THEN** the next valid update starts a fresh healthy batch

### Requirement: Clock separation and metadata preservation
The system SHALL use monotonic local receive time for the 100us boundary while
retaining the last real exchange event time and receive time on every emitted
candidate.

#### Scenario: Timer closes a batch
- **WHEN** a monotonic one-shot timer closes an eligible batch
- **THEN** emitted candidates retain their original exchange timestamps and record the later batch-emission timestamp separately

### Requirement: Standard SSE tick sample gate
After batch-end eligibility, the system SHALL emit a tick-model factor row only
for a valid continuous-trading cut with a strictly increasing cut index that
meets at least one frozen turnover, exchange-time, or price/volume trigger.

#### Scenario: Turnover trigger
- **WHEN** window turnover is greater than or equal to the instrument turnover threshold at an eligible batch-end cut
- **THEN** the sample is accepted and the sampling window starts again at that cut

#### Scenario: Time trigger
- **WHEN** at least 100 seconds of exchange time have elapsed since the window start at an eligible batch-end cut
- **THEN** the sample is accepted

#### Scenario: Change trigger
- **WHEN** the mid price changes and cumulative volume increases by at least 100 shares at an eligible batch-end cut
- **THEN** the sample is accepted

#### Scenario: No standard trigger
- **WHEN** an eligible batch-end cut meets none of the turnover, time, or change triggers
- **THEN** no model factor row is emitted and the window start remains unchanged

### Requirement: Prediction-only Shanghai configuration
The repository SHALL provide a dated Shanghai configuration that selects the
native Snapshot model before 09:35, the warmed native tick model from 09:35,
strict live batch-end sampling, fail-closed feed handling, and disabled order
routing.

#### Scenario: Configuration validation
- **WHEN** the generated Shanghai config is checked before deployment
- **THEN** all model paths, routing windows, threshold semantics, clock roles, and deployment placeholders are explicit and no secret is embedded
