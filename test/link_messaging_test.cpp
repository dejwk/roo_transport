#include "roo_transport/link/link_messaging.h"

#include "gtest/gtest.h"
#include "helpers/link_loopback.h"
#include "helpers/rand.h"
#include "roo_transport/messaging/mux_messaging.h"

namespace roo_transport {

class Message {
 public:
  Message(const char* data) : Message(data, strlen(data) + 1) {}

  Message(const void* data, size_t size)
      : data_(new roo::byte[size]), size_(size) {
    std::memcpy(data_.get(), data, size);
  }

  const void* data() const { return data_.get(); }
  size_t size() const { return size_; }

  bool operator==(const Message& other) const {
    if (size_ != other.size_) return false;
    return std::memcmp(data_.get(), other.data_.get(), size_) == 0;
  }

  friend void PrintTo(const Message& msg, std::ostream* os) {
    *os << (const char*)msg.data_.get();
  }

 private:
  std::shared_ptr<roo::byte[]> data_;
  size_t size_;
};

class MessagingTester {
 public:
  MessagingTester(Messaging& server, Messaging& client)
      : server_receiver_(
            [this](roo_transport::Messaging::ConnectionId ignored_connection_id,
                   const void* data,
                   size_t len) { serverReceived(data, len); }),
        client_receiver_(
            [this](roo_transport::Messaging::ConnectionId ignored_connection_id,
                   const void* data,
                   size_t len) { clientReceived(data, len); }),
        server_(server),
        client_(client) {
    server_.setReceiver(server_receiver_);
    client_.setReceiver(client_receiver_);
  }

  ~MessagingTester() = default;

  void serverReceived(const void* data, size_t len) {
    roo::lock_guard<roo::mutex> guard(mutex_);
    if (len == 0) {
      server_fin_received_ = true;
      done_.notify_all();
      return;
    }
    server_received_.emplace_back(data, len);
  }

  void clientReceived(const void* data, size_t len) {
    roo::lock_guard<roo::mutex> guard(mutex_);
    if (len == 0) {
      client_fin_received_ = true;
      done_.notify_all();
      return;
    }
    client_received_.emplace_back(data, len);
  }

  // Should be called only after join().
  const std::vector<Message>& serverReceived() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return server_received_;
  }

  // Should be called only after join().
  const std::vector<Message>& clientReceived() const {
    roo::lock_guard<roo::mutex> guard(mutex_);
    return client_received_;
  }

  void join() {
    roo::unique_lock<roo::mutex> guard(mutex_);
    while (!server_fin_received_ || !client_fin_received_) {
      done_.wait(guard);
    }
  }

  Messaging& server() { return server_; }

  Messaging& client() { return client_; }

 protected:
  Messaging::SimpleReceiver server_receiver_;
  Messaging::SimpleReceiver client_receiver_;
  Messaging& server_;
  Messaging& client_;

  mutable roo::mutex mutex_;
  roo::condition_variable done_;
  bool server_fin_received_ = false;
  bool client_fin_received_ = false;

  std::vector<Message> server_received_;
  std::vector<Message> client_received_;
};

class LoopbackTestBase : public testing::Test {
 public:
  LoopbackTestBase()
      : loopback_(),
        server_(loopback_.server(), 1500),
        client_(loopback_.client(), 1500) {}

  void begin() {
    server_.begin();
    client_.begin();
  }

  void end() {
    server_.end();
    client_.end();
  }

  ~LoopbackTestBase() override { loopback_.close(); }

 protected:
  LinkLoopback loopback_;
  LinkMessaging server_;
  LinkMessaging client_;
};

class SimpleLinkMessagingTest : public LoopbackTestBase,
                                public MessagingTester {
 public:
  SimpleLinkMessagingTest()
      : LoopbackTestBase(),
        MessagingTester(LoopbackTestBase::server_, LoopbackTestBase::client_) {
    begin();
  }

  ~SimpleLinkMessagingTest() override { end(); }
};

class MuxMessagingTester {
 public:
  MuxMessagingTester(Messaging& server, Messaging& client,
                     MuxMessaging::ChannelId channel_id)
      : server_(server),
        client_(client),
        channel_server_(server_, channel_id),
        channel_client_(client_, channel_id) {}

 protected:
  roo_transport::MuxMessaging server_;
  roo_transport::MuxMessaging client_;
  roo_transport::MuxMessaging::Channel channel_server_;
  roo_transport::MuxMessaging::Channel channel_client_;
};

class MuxMessagingTest : public LoopbackTestBase,
                         public MuxMessagingTester,
                         public MessagingTester {
 public:
  MuxMessagingTest()
      : LoopbackTestBase(),
        MuxMessagingTester(LoopbackTestBase::server_, LoopbackTestBase::client_,
                           23),
        MessagingTester(channel_server_, channel_client_) {
    begin();
  }

  ~MuxMessagingTest() override { end(); }
};

TEST_F(SimpleLinkMessagingTest, ConstructionDestruction) {
  // Nothing to do, just testing that construction and destruction works.
}

TEST_F(SimpleLinkMessagingTest, SendReceiveOneEach) {
  server().send((const roo::byte*)"Hello, World!", 14);
  client().send((const roo::byte*)"Hello back!", 12);
  server().send(nullptr, 0);
  client().send(nullptr, 0);
  join();
  EXPECT_EQ(serverReceived(), std::vector<Message>{"Hello back!"});
  EXPECT_EQ(clientReceived(), std::vector<Message>{"Hello, World!"});
}

TEST_F(MuxMessagingTest, SendReceiveOneEach) {
  server().send((const roo::byte*)"Hello, World!", 14);
  client().send((const roo::byte*)"Hello back!", 12);
  server().send(nullptr, 0);
  client().send(nullptr, 0);
  join();
  EXPECT_EQ(serverReceived(), std::vector<Message>{"Hello back!"});
  EXPECT_EQ(clientReceived(), std::vector<Message>{"Hello, World!"});
}

}  // namespace roo_transport