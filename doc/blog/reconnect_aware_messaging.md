# Reset-Tolerant Command Channels Between Boards

Imagine a UI controller board talking to a Wi-Fi board, a power-management
board, or a sensor hub.

The traffic usually does not look like a stream. It looks like commands and
events:

- "set brightness to 80%";
- "button 3 pressed";
- "wifi credentials updated";
- "here is the latest state snapshot".

This sounds easier than a transport problem until resets enter the picture. One
board reboots after receiving a command but before replying. A late response
shows up after reconnection. Two different subsystems want to share the same
physical link. Suddenly the "simple command protocol" is carrying a lot more
state than expected.

## Why this problem is not trivial to fix

Once you build command exchange on top of a raw stream, you end up owning all
the hard parts yourself:

- where one command ends and the next begins;
- how replies are matched to the connection they belong to;
- what happens if the peer resets between request and response;
- how multiple subsystems share the same underlying transport.

The tricky part is not serializing the payload. The tricky part is deciding
which messages still belong to the old conversation after a reset and which ones
must be discarded. That is where many homemade command channels get subtle and
fragile.

## The roo_transport-based fix

If your application naturally thinks in whole commands or records, the right
solution is to use a message transport instead of rebuilding record handling on
top of a stream.

`LinkMessaging` gives you whole messages over a reliable link. One send on one
side produces one received message on the other side. You stop thinking about
delimiters and start thinking about commands again.

## Why messaging is the sweet spot for many projects

Messaging is often the best layer when:

- your payloads are already natural records;
- you want ordered, reliable delivery while the connection is healthy;
- the application must notice reconnects and clear session-specific state;
- RPC would be overkill because your protocol is still message-oriented.

The setup is small:

```cpp
#include "roo_transport/link/link_messaging.h"

using namespace roo_transport;

constexpr int kMaxPayloadSize = 32;

LinkMessaging messaging(reliable_serial, kMaxPayloadSize);

Messaging::SimpleReceiver receiver(
    [](Messaging::ConnectionId connection_id, const roo::byte* data,
       size_t len) {
      // Decode one complete message here.
    });

void startMessaging() {
  messaging.setReceiver(receiver);
  messaging.begin();
  messaging.send((const roo::byte*)"ping", 4);
}
```

The main thing to notice is not the syntax. It is the contract: one logical
message in, one logical message out.

## Why reconnect handling is better here than in custom code

Messaging reliability only applies while the connection is healthy. That is the
important nuance.

If the peer resets, the current conversation ends. Messages that were already
delivered stay delivered. Messages that were supposed to be part of the old
conversation must not silently attach themselves to a new one.

That is why the API exposes `ConnectionId` and `sendContinuation()`.

- `ConnectionId` identifies the current logical connection instance.
- `sendContinuation(connection_id, ...)` lets you reply on that same logical
  connection.
- if a reconnect happened in the meantime, the continuation send fails instead
  of sending a stale reply on the wrong session.

That behavior is exactly what you want for command channels. It is much safer
than pretending a board reset never happened.

## RAM and scaling tradeoffs are explicit

`LinkMessaging` asks you for `max_recv_packet_size`. That is not busywork; it
is the real RAM tradeoff. The receive buffer must hold your largest incoming
message, so large values cost memory.

The library keeps that tradeoff explicit instead of hiding it behind dynamic
allocation surprises. That is the right shape for embedded work.

If you later want several logical protocols over one physical link, you do not
need to open separate transports. Use
[Messaging multiplexing](../examples/Messaging/Multiplexing/Multiplexing.ino)
and add `MuxMessaging` channels on top of the same messaging transport.

## Where to start

Start with the
[Messaging simple example](../examples/Messaging/Simple/Simple.ino).
It demonstrates the intended model clearly: both sides exchange small messages,
and reconnects are treated as part of the system's reality rather than as an
edge case to ignore.

If your messages are already starting to look like function calls with typed
requests and responses, that is the sign to move up one more layer to
[RPC for coprocessors](rpc_for_coprocessors.md).