# Unit testing

## Inject narrow capabilities

Dependency injection is most useful when the injected interfaces are small and describe
specific **capabilities** rather than exposing a large owner object or a generic I/O facade.
A component's constructor should state the external effects it is allowed to perform, such
as sending a particular family of messages, updating the data model, or scheduling a timer.
The component should not depend on unrelated operations just because they are available
from the same database, controller, or runtime object.

Such an interface can be viewed as a small effect algebra: it declares the operations used
by the component without defining how they are performed. Production code interprets those
operations as real I/O, while a test implementation can record calls or return controlled
results. This provides the useful part of an effect-system or free-monad-style separation
without requiring either abstraction to be implemented literally in C++.

Prefer capabilities expressed at the level of intent, such as `send_topology_query()`, over
capabilities that expose the underlying mechanism. This keeps both production wiring and
test doubles small, and prevents tests from reproducing transport implementation details.

## Future event-loop design

The following recommendations concern a future redesign of the shared prplMesh event-loop
and timer infrastructure.
They are not requirements for an incremental refactoring of any existing polling task.
Until this infrastructure exists, injecting an interface to query clock
remains a pragmatic way to make time-dependent code testable.

### Prefer timestamped events to querying the clock

Injecting a clock is a pragmatic way to test code that currently polls state and compares
deadlines with `now`. In a redesigned event loop, an event should instead carry the timestamp
at which it occurred, and a timer expiration should arrive as an event rather than require
the consumer to repeatedly query a clock.

This makes time another input to the state transition and removes a hidden dependency on
the event loop's or system's clock. It also lets tests provide timestamps directly. Event
time and processing time can differ when an event waits in a queue; code that depends on the
distinction should define which one it uses. For current prplMesh control-plane tasks this
difference is generally not important, so the event occurrence time should normally be
sufficient.

### Own timer registrations

The current `TimerManager` returns a file descriptor from `add_timer()` and requires callers
to pass it back to `remove_timer()`. The descriptor is an implementation detail and manual
cleanup is easy to miss on an early return or during object destruction.

A redesigned timer API should support both interval and single-shot timers and return a
`std::unique_ptr` to an abstract timer registration handle for either kind. Destroying the
handle should cancel the timer and unregister its callback. A handle does not need a separate
file descriptor: the event loop can keep registrations ordered by deadline and wait for
either I/O or the earliest timer. This keeps cancellation scoped through RAII, hides the
timer implementation, and gives tests a timer capability that can store callbacks and
trigger timestamped expiration events without using the kernel clock.

## Keep expectations as loose as the contract allows

Tests should constrain only behavior that matters to the contract. For example, if a
Topology Query only has to be sent during a call to `work()`, check that the destination is
present in the recorded queries rather than requiring it to be at the front of a queue.
Assert call order, an exact call count, or the absence of unrelated calls only when that
detail is part of the behavior under test.

Mocks that prescribe interactions often test implementation details rather than observable
behavior, making tests fragile under harmless refactoring. Mock frameworks encourage such
strict expectations because every expected interaction is specified up front. Relaxed mocks
and expectations such as "at least once" can reduce this coupling, but a small fake that
records operations often makes the intended assertion more direct. Loose expectations keep
tests valid when the implementation batches, reorders, or adds independent work without
changing its observable contract.

## Use real lightweight dependencies

Not every dependency needs a test double. The local data model is lightweight enough to use
directly in unit tests, and doing so exercises its real object and parameter behavior while
avoiding a large mocked interface.

## IEEE1905 task example

The `ieee1905_task` unit tests demonstrate the capability approach for message transmission.
External transmission is kept behind `IEEE1905QuerySender`. Production code injects
`RealIEEE1905QuerySender`, while the test injects `FakeIEEE1905QuerySender`, which records the
destination of each Topology, Higher Layer, and Link Metric query in separate queues.

The task owns the injected sender through a `std::unique_ptr`. The fixture keeps a non-owning
pointer to the fake before transferring ownership, then inspects its queues after calling the
task:

```cpp
auto query_sender = std::make_unique<FakeIEEE1905QuerySender>();
m_query_sender    = query_sender.get();

m_task = std::make_unique<TestableIEEE1905Task>(
    database, cmdu_tx, std::move(query_sender), [this]() { return m_now; });

m_task->work();
EXPECT_TRUE(topology_query_sent_to(destination));
```

Time is currently injected separately through `ieee1905_task::now_f`. Tests advance the
fixture's `m_now` value and call `work()` explicitly, making interval boundaries and timeout
behavior deterministic without wall-clock delays. This clock injection is a testing seam for
the existing polling task, not an intended replacement for the broader event-loop and timer
redesign described above.
