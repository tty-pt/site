# axil-nd-bus

`nd-bus` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Provides the decoupled cross-module event bus and signal dispatch for the NeverDark engine.

## Role & Subsystem

`nd-bus` allows game modules to publish and subscribe to domain events (entity creation, movement, combat rounds, environmental ticks, destruction) without tight compile-time coupling.

## Key Capabilities

- **Event Registration**: Dynamic registration of named event topics.
- **Dispatch Chains**: Priority-ordered listener queues integrated with libxylem dispatch.
- **Zero-Copy Payload Forwarding**: Passes pointer references through the event bus to avoid allocations on high-frequency game ticks.

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-bus.so`.
