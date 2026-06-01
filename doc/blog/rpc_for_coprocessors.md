# Treat a Coprocessor Like a Service

Suppose your main MCU delegates one subsystem to another board.

Maybe the second MCU owns calibration logic, a radio, a display stack, sensor
fusion, or a configuration store. Very quickly the calling code starts wanting
operations such as:

- `getTemperature()`
- `readConfig()`
- `setBrightness()`
- `runCalibration()`

That is usually the point where a message protocol starts pretending to be an
API. The trouble is that function-shaped communication is much harder to fake
cleanly than it first appears.

## Why this problem is not trivial to fix

A lot of homemade embedded "RPC" code is really command messaging plus a growing
pile of conventions:

- a numeric opcode table;
- one serialization format for requests and another for responses;
- a map of in-flight requests;
- retry logic after reconnects;
- ad hoc error handling.

None of that is conceptually hard, but getting all of it right at once is what
makes these systems expensive to maintain. Overlapping requests, timeouts,
retries, and board resets turn a small opcode protocol into infrastructure.

## The roo_transport-based fix

If the remote side really looks like a service, the cleanest solution is to use
an RPC layer that already has the transport and reset semantics defined.

`roo_transport` already has that stack. `RpcServer`, `RpcClient`,
`UnaryHandler`, and `UnaryStub` let you define a service in terms that look much
closer to the application you are actually building.

## Why this works well for coprocessor designs

The RPC layer sits on top of reconnect-aware messaging, so it inherits the most
important behavior: failed transport sessions are explicit.

That means you get a clean rule for resets:

- if the server resets during a request, that request fails;
- the client sees `kUnavailable` for that call;
- the overall system stays alive;
- your application can retry the call if retrying makes sense.

That is a much better model than silently replaying old requests on a new
connection or pretending a rebooted board still has the same in-flight state.

The simple server-side shape is small:

```cpp
enum FunctionId {
  kSquareFn = 0,
};

RpcStatus calcSquare(uint32_t x, uint32_t& result) {
  result = x * x;
  return kOk;
}

FunctionTable rpc_function_table = {
    {kSquareFn, UnaryHandler<uint32_t, uint32_t>(calcSquare)},
};
```

And the client side is equally direct:

```cpp
RpcClient rpc_client(client_messaging);
UnaryStub<uint32_t, uint32_t> square_stub(rpc_client, kSquareFn);

uint32_t response;
RpcStatus status = square_stub.call(12, response);
```

The point is not that squaring integers is useful. The point is that your
coprocessor API can look like typed C++ calls rather than like a packet parser.

## This is where the transport performance matters

RPC only feels natural when the underlying link is fast enough and predictable
enough that remote calls do not become a last resort.

That is why the benchmark numbers matter here more than anywhere else in the
library. On ESP32, `roo_transport` currently reports about **3.4 Mbps** over
short wires at 5 Mbps UART with sub-millisecond round-trip latency, and about
**2.5 Mbps** with **0.92 ms p99 round-trip latency** over a **7.5 m
unshielded four-wire cable** under heavy packet loss.

Those numbers do not turn every remote call into a local one, but they do make
RPC a practical control-plane tool between microcontrollers.

## You are not locked into synchronous handlers

Many embedded services are not instant. Calibration, flash access, peripheral
probing, or sensor stabilization may take time.

The library does not force you into a synchronous-only model.

- use `UnaryStub::call()` when a blocking request/response flow is fine;
- use `callAsync()` when the client should keep working while the response is
  pending;
- use `AsyncUnaryHandler` when the server-side operation completes later.

That flexibility is important if you want the convenience of RPC without giving
up event-driven application structure.

## Where to start

Start with the
[RPC simple example](../examples/Rpc/Simple/Simple.ino).
It shows the key operational promise very clearly: if the server resets during
a request, that call fails, but the system as a whole keeps going.

Then move to:

- [RPC serialization](../examples/Rpc/Serialization/Serialization.ino) for
  real request and response types;
- [RPC asynchronous](../examples/Rpc/Asynchronous/Asynchronous.ino) when work
  does not finish immediately.

If your protocol does not really need request/response semantics and is closer
to fire-and-forget commands or state updates, the lighter entry point is
[messaging](reconnect_aware_messaging.md).