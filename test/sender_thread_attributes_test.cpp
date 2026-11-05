#include <array>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gtest/gtest.h"
#include "roo_threads/condition_variable.h"
#include "roo_threads/mutex.h"
#include "roo_threads/thread.h"
#include "roo_time.h"
#include "roo_transport/link/internal/thread_safe/channel.h"
#include "roo_transport/link/link_transport.h"

namespace roo_transport {
namespace {

struct SenderTaskObservation {
  TaskHandle_t task = nullptr;
  std::string name;
  UBaseType_t priority = 0;
};

/// Observes the real sender task and optionally delivers packets to a peer.
class ObservingPacketSender : public PacketSender {
 public:
  /// Sets the peer before either transport starts its worker.
  void setPeer(LinkTransport& peer) { peer_ = &peer; }

  /// Captures task attributes before taking locks that can inherit priority.
  void send(const roo::byte* bytes, size_t count) override {
    SenderTaskObservation observation;
    observation.task = xTaskGetCurrentTaskHandle();
    observation.name = pcTaskGetName(observation.task);
    observation.priority = uxTaskPriorityGet(observation.task);
    if (peer_ != nullptr) peer_->processIncomingPacket(bytes, count);
    roo::lock_guard<roo::mutex> guard(mutex_);
    if (!observed_) {
      first_send_ = observation;
      observed_ = true;
    }
    changed_.notify_all();
  }

  /// Waits for a send with a finite failure bound and copies its observation.
  bool awaitSend(SenderTaskObservation& observation) {
    roo::unique_lock<roo::mutex> guard(mutex_);
    if (!changed_.wait_for(guard, roo_time::Seconds(5),
                           [this]() { return observed_; })) {
      return false;
    }
    observation = first_send_;
    return true;
  }

  /// Reads the requested payload with a finite bound, waking after delivery.
  bool awaitRead(LinkInputStream& input, roo::byte* bytes, size_t count) {
    roo::unique_lock<roo::mutex> guard(mutex_);
    size_t total_read = 0;
    return changed_.wait_for(guard, roo_time::Seconds(5), [&]() {
      total_read += input.tryRead(bytes + total_read, count - total_read);
      return total_read == count;
    });
  }

 private:
  LinkTransport* peer_ = nullptr;
  roo::mutex mutex_;
  roo::condition_variable changed_;
  bool observed_ = false;
  SenderTaskObservation first_send_;
};

/// Keeps both transports alive until both sending workers have stopped.
class TransportPair {
 public:
  /// Creates a lossless packet connection without a separate receiver thread.
  TransportPair()
      : first(first_sender, "first"), second(second_sender, "second") {
    first_sender.setPeer(second);
    second_sender.setPeer(first);
  }

  /// Stops both workers before either peer or packet sender is destroyed.
  ~TransportPair() {
    first.end();
    second.end();
  }

  ObservingPacketSender first_sender;
  ObservingPacketSender second_sender;
  LinkTransport first;
  LinkTransport second;
};

// Verifies that no-argument begin retains the unnamed and diagnostic-derived
// task names and the existing FreeRTOS sender priority.
TEST(SenderThreadAttributesTest, ChannelDefaultPreservesNameAndPriority) {
  struct NameCase {
    const char* diagnostic_name;
    const char* task_name;
  };
  constexpr std::array<NameCase, 2> kNames = {
      {{"", "send_loop"}, {"pump", "pump-send"}}};
  for (const NameCase& names : kNames) {
    SCOPED_TRACE(names.diagnostic_name);
    ObservingPacketSender sender;
    Channel channel(sender, kBufferSize4KB, kBufferSize4KB,
                    names.diagnostic_name);
    channel.connect();
    channel.begin();
    SenderTaskObservation observation;
    ASSERT_TRUE(sender.awaitSend(observation));
    EXPECT_NE(observation.task, nullptr);
    EXPECT_NE(observation.task, xTaskGetCurrentTaskHandle());
    EXPECT_EQ(observation.name, names.task_name);
    EXPECT_EQ(observation.priority,
              static_cast<UBaseType_t>(configMAX_PRIORITIES - 2));
  }
}

// Verifies that explicit attributes reach the actual Channel worker, including
// the caller's name instead of the diagnostic-derived default.
TEST(SenderThreadAttributesTest, ChannelUsesExplicitNameAndPriority) {
  ObservingPacketSender sender;
  Channel channel(sender, kBufferSize4KB, kBufferSize4KB, "diagnostic");
  roo::thread::attributes attributes;
  attributes.set_name("custom-sender");
  attributes.set_priority(1);
  attributes.set_stack_size(8192);
  channel.connect();
  channel.begin(attributes);
  SenderTaskObservation observation;
  ASSERT_TRUE(sender.awaitSend(observation));
  EXPECT_NE(observation.task, nullptr);
  EXPECT_NE(observation.task, xTaskGetCurrentTaskHandle());
  EXPECT_EQ(observation.name, "custom-sender");
  EXPECT_EQ(observation.priority, 1u);
}

// Verifies that LinkTransport forwards explicit attributes to its worker while
// completing the real handshake and delivering ordered payloads both ways.
TEST(SenderThreadAttributesTest,
     LinkTransportAttributesPreserveBidirectionalFlow) {
  TransportPair pair;
  roo::thread::attributes attributes;
  attributes.set_name("link-sender");
  attributes.set_priority(2);
  attributes.set_stack_size(12288);
  Link first = pair.first.connectAsync();
  Link second = pair.second.connectAsync();
  pair.first.begin(attributes);
  pair.second.begin();
  ASSERT_TRUE(first.awaitConnected(roo_time::Seconds(5)));
  ASSERT_TRUE(second.awaitConnected(roo_time::Seconds(5)));
  ASSERT_EQ(first.status(), LinkStatus::kConnected);
  ASSERT_EQ(second.status(), LinkStatus::kConnected);

  SenderTaskObservation observation;
  ASSERT_TRUE(pair.first_sender.awaitSend(observation));
  EXPECT_NE(observation.task, xTaskGetCurrentTaskHandle());
  EXPECT_EQ(observation.name, "link-sender");
  EXPECT_EQ(observation.priority, 2u);
  ASSERT_TRUE(pair.second_sender.awaitSend(observation));
  EXPECT_EQ(observation.name, "second-send");
  EXPECT_EQ(observation.priority,
            static_cast<UBaseType_t>(configMAX_PRIORITIES - 2));

  std::array<roo::byte, 600> sent;
  for (size_t i = 0; i < sent.size(); ++i) {
    sent[i] = static_cast<roo::byte>(i * 73 + 19);
  }
  ASSERT_EQ(first.out().tryWrite(sent.data(), sent.size()), sent.size());
  first.out().flush();
  std::array<roo::byte, 600> received;
  ASSERT_TRUE(pair.first_sender.awaitRead(second.in(), received.data(),
                                          received.size()));
  EXPECT_EQ(received, sent);

  ASSERT_EQ(second.out().tryWrite(sent.data(), sent.size()), sent.size());
  second.out().flush();
  ASSERT_TRUE(pair.second_sender.awaitRead(first.in(), received.data(),
                                           received.size()));
  EXPECT_EQ(received, sent);
}

}  // namespace
}  // namespace roo_transport
