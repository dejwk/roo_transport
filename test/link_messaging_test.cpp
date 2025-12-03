#include "roo_transport/link/link_messaging.h"

#include "gtest/gtest.h"
#include "helpers/link_loopback.h"
#include "helpers/rand.h"

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

class SimpleLinkMessagingTest : public ::testing::Test {
 public:
  SimpleLinkMessagingTest()
      : loopback_(),
        server_receiver_([this](roo_transport::Messaging::ConnectionId ignored,
                                const void* data,
                                size_t len) { serverReceived(data, len); }),
        client_receiver_([this](roo_transport::Messaging::ConnectionId ignored,
                                const void* data,
                                size_t len) { clientReceived(data, len); }),
        server_(loopback_.server(), 1500),
        client_(loopback_.client(), 1500),
        server_channel_(server_, 23),
        client_channel_(client_, 23) {
    server_channel_.setReceiver(server_receiver_);
    client_channel_.setReceiver(client_receiver_);
    server_.begin();
    client_.begin();
  }

  ~SimpleLinkMessagingTest() override {
    server_.end();
    client_.end();
    loopback_.close();
  }

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

  Messaging::Channel& serverChannel() { return server_channel_; }

  Messaging::Channel& clientChannel() { return client_channel_; }

 protected:
  LinkLoopback loopback_;
  Messaging::SimpleReceiver server_receiver_;
  Messaging::SimpleReceiver client_receiver_;
  LinkMessaging server_;
  LinkMessaging client_;
  Messaging::Channel server_channel_;
  Messaging::Channel client_channel_;

  mutable roo::mutex mutex_;
  roo::condition_variable done_;
  bool server_fin_received_ = false;
  bool client_fin_received_ = false;

  std::vector<Message> server_received_;
  std::vector<Message> client_received_;
};

TEST_F(SimpleLinkMessagingTest, ConstructionDestruction) {
  // Nothing to do, just testing that construction and destruction works.
}

TEST_F(SimpleLinkMessagingTest, SendReceiveOneEach) {
  serverChannel().send((const roo::byte*)"Hello, World!", 14);
  clientChannel().send((const roo::byte*)"Hello back!", 12);
  serverChannel().send(nullptr, 0);
  clientChannel().send(nullptr, 0);
  join();
  EXPECT_EQ(serverReceived(), std::vector<Message>{"Hello back!"});
  EXPECT_EQ(clientReceived(), std::vector<Message>{"Hello, World!"});
}

}  // namespace roo_transport