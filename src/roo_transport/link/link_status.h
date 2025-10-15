#pragma once

namespace roo_transport {

enum LinkStatus {
  // The link is idle, no connection being established. This is an initial state
  // of a default-constructed link.
  kIdle,

  // The link is in process of establishing a connection with the peer.
  kConnecting,

  // The link is established and ready for data transfer.
  kConnected,

  // The link is broken and no longer usable.
  kBroken,
};

}  // namespace roo_transport