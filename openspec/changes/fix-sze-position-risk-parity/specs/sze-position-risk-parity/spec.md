## ADDED Requirements

### Requirement: Shenzhen buy and sell capacity SHALL match T0 inventory semantics
The Shenzhen strategy SHALL cap buy capacity by both remaining sellable inventory and the configured position limit, and SHALL cap sell capacity by sellable inventory and actual holdings after pending orders.

#### Scenario: Baseline inventory is not sellable
- **WHEN** total position equals `static_position` and broker-reported available position is zero
- **THEN** additional Shenzhen buy and sell capacity SHALL both be zero

#### Scenario: Zero position is below baseline
- **WHEN** total and available positions are zero while `static_position` is positive
- **THEN** buy capacity SHALL allow restoration toward the baseline through the negative position delta
- **AND** sell capacity SHALL remain zero

### Requirement: Startup synchronization SHALL preserve position-field relationships
The Shenzhen strategy SHALL set `pi` and `last_position` to total position minus `static_position`, and SHALL set `shortable` from the clamped broker available position.

#### Scenario: Live position differs from configured baseline
- **WHEN** a startup position response provides total and available quantities
- **THEN** `static_position + pi` SHALL equal total position
- **AND** `last_position` SHALL equal `pi`
- **AND** `shortable` SHALL equal available position clamped to total position

### Requirement: Missing startup positions SHALL remain gated until cutoff
The Shenzhen live strategy SHALL retry incomplete startup position queries and SHALL NOT immediately treat omitted instruments as zero. It SHALL default unresolved instruments to zero only after the configured cutoff.

#### Scenario: Position response omits an instrument before cutoff
- **WHEN** an authoritative final callback omits a configured instrument before cutoff
- **THEN** that instrument SHALL remain unable to route orders
- **AND** another all-position query SHALL be scheduled

#### Scenario: Instrument remains unresolved at cutoff
- **WHEN** the configured cutoff is reached and an instrument has not appeared in any valid startup position response
- **THEN** the instrument SHALL be synchronized to total zero and available zero
- **AND** the zero default SHALL be logged for audit

### Requirement: Shenzhen bias SHALL use the fixed Beijing baseline
The Shenzhen theoretical-price bias SHALL use current position notional divided by `position_base_line`, multiplied by the configured baseline bias factor, without an implicit time-of-day multiplier.

#### Scenario: Afternoon signal uses configured bias factor
- **WHEN** equivalent morning and afternoon states have the same price and position
- **THEN** Shenzhen SHALL compute the same bias and unit-bias values for both states
