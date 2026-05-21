#include "roo_transport/link/link_transport.h"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <memory>

#include "gtest/gtest.h"
#include "helpers/link_loopback.h"
#include "helpers/rand.h"
#include "roo_threads/atomic.h"
#include "roo_threads/mutex.h"
#include "roo_threads/thread.h"
#include "roo_time.h"
#include "roo_transport/link/link_transport.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"
namespace roo_transport {

namespace {
class ConfigurableLinkLoopback {
 public:
  ConfigurableLinkLoopback(LinkBufferSize server_sendbuf,
                           LinkBufferSize server_recvbuf,
                           LinkBufferSize client_sendbuf,
                           LinkBufferSize client_recvbuf,
                           size_t client_to_server_pipe_capacity = 4096,
                           size_t server_to_client_pipe_capacity = 4096)
      : pipe_client_to_server_(client_to_server_pipe_capacity),
        pipe_server_to_client_(server_to_client_pipe_capacity),
        server_input_(pipe_client_to_server_),
        server_output_(pipe_server_to_client_),
        noisy_server_output_(server_output_, 0),
        client_input_(pipe_server_to_client_),
        client_output_(pipe_client_to_server_),
        noisy_client_output_(client_output_, 0),
        server_packet_sender_(noisy_server_output_),
        server_packet_receiver_(server_input_),
        client_packet_sender_(noisy_client_output_),
        client_packet_receiver_(client_input_),
        server_(server_packet_sender_, server_sendbuf, server_recvbuf),
        client_(client_packet_sender_, client_sendbuf, client_recvbuf) {
    begin();
  }

  ~ConfigurableLinkLoopback() {
    close();
    if (server_receiving_thread_.joinable()) {
      server_receiving_thread_.join();
    }
    if (client_receiving_thread_.joinable()) {
      client_receiving_thread_.join();
    }
  }

  LinkTransport& server() { return server_; }
  LinkTransport& client() { return client_; }

 private:
  bool serverReceive() {
    if (server_input_.status() != roo_io::kOk) return false;
    server_packet_receiver_.receive([this](const roo::byte* buf, size_t len) {
      server_.processIncomingPacket(buf, len);
    });
    return true;
  }

  bool clientReceive() {
    if (client_input_.status() != roo_io::kOk) return false;
    client_packet_receiver_.receive([this](const roo::byte* buf, size_t len) {
      client_.processIncomingPacket(buf, len);
    });
    return true;
  }

  void begin() {
    server_.begin();
    client_.begin();

    roo::thread::attributes server_attrs;
    server_attrs.set_name("cfg server recv");
    server_receiving_thread_ = roo::thread(server_attrs, [this]() {
      while (serverReceive()) {
      }
    });

    roo::thread::attributes client_attrs;
    client_attrs.set_name("cfg client recv");
    client_receiving_thread_ = roo::thread(client_attrs, [this]() {
      while (clientReceive()) {
      }
    });
  }

  void close() {
    server_output_.close();
    client_output_.close();
  }

  roo_io::RingPipe pipe_client_to_server_;
  roo_io::RingPipe pipe_server_to_client_;
  roo_io::RingPipeInputStream server_input_;
  roo_io::RingPipeOutputStream server_output_;
  NoisyOutputStream noisy_server_output_;
  roo_io::RingPipeInputStream client_input_;
  roo_io::RingPipeOutputStream client_output_;
  NoisyOutputStream noisy_client_output_;
  PacketSenderOverStream server_packet_sender_;
  PacketReceiverOverStream server_packet_receiver_;
  PacketSenderOverStream client_packet_sender_;
  PacketReceiverOverStream client_packet_receiver_;
  LinkTransport server_;
  LinkTransport client_;
  roo::thread server_receiving_thread_;
  roo::thread client_receiving_thread_;
};

#if defined(__unix__)
constexpr char kTransportScenarioEnv[] = "ROO_LINK_TRANSPORT_SCENARIO";

void ExpectSubprocessSuccess(const char* scenario_name,
                             unsigned alarm_seconds = 3) {
  pid_t pid = fork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
    setenv(kTransportScenarioEnv, scenario_name, 1);
    execl("/proc/self/exe", "/proc/self/exe",
          "--gtest_filter=LinkTransportSubprocess.RunScenario",
          static_cast<char*>(nullptr));
    _exit(127);
  }
  int status = 0;
  pid_t waited = -1;
  roo_time::Uptime deadline =
      roo_time::Uptime::Now() + roo_time::Millis(alarm_seconds * 1000);
  while (true) {
    waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      break;
    }
    if (waited == -1) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (roo_time::Uptime::Now() >= deadline) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      FAIL() << "child timed out";
    }
    usleep(1000);
  }
  ASSERT_EQ(waited, pid);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0)
      << (WIFSIGNALED(status) ? std::string("child terminated by signal ") +
                                    std::to_string(WTERMSIG(status))
                              : std::string("child exited with code ") +
                                    std::to_string(WEXITSTATUS(status)));
}

int RunQuiescentSmallSendBufferScenario() {
  ConfigurableLinkLoopback loopback(kBufferSize256KB, kBufferSize256KB,
                                    kBufferSize256B, kBufferSize256KB);

  Link server = loopback.server().connectAsync();
  Link client = loopback.client().connect();
  if (!server.awaitConnected(roo_time::Millis(500)) ||
      server.status() != LinkStatus::kConnected ||
      client.status() != LinkStatus::kConnected) {
    return 10;
  }

  // Let the sender thread go quiescent before writing.
  roo::this_thread::sleep_for(roo_time::Millis(150));

  std::vector<roo::byte> payload(900);
  for (size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<roo::byte>(i & 0xFF);
  }

  roo::atomic<bool> done(false);
  roo::thread writer([&]() {
    client.out().writeFully(payload.data(), payload.size());
    done = true;
  });

  roo_time::Uptime deadline = roo_time::Uptime::Now() + roo_time::Millis(200);
  while (!done && roo_time::Uptime::Now() < deadline) {
    roo::this_thread::sleep_for(roo_time::Millis(1));
  }
  if (!done) {
    _exit(11);
  }

  writer.join();
  client.out().close();
  return 0;
}

int RunDisconnectCallbackReenterScenario() {
  LinkLoopback loopback;

  Link server = loopback.server().connectAsync();

  roo::mutex mutex;
  roo::condition_variable callback_done_cv;
  bool callback_done = false;
  std::unique_ptr<Link> reconnected;

  Link client = loopback.client().connect([&]() {
    auto next = std::make_unique<Link>(loopback.client().connectAsync());
    roo::lock_guard<roo::mutex> guard(mutex);
    reconnected = std::move(next);
    callback_done = true;
    callback_done_cv.notify_all();
  });
  if (!server.awaitConnected(roo_time::Millis(500)) ||
      server.status() != LinkStatus::kConnected ||
      client.status() != LinkStatus::kConnected) {
    return 20;
  }

  Link replacement = loopback.server().connect();
  if (replacement.status() != LinkStatus::kConnected) {
    return 21;
  }

  {
    roo::unique_lock<roo::mutex> guard(mutex);
    roo_time::Uptime deadline = roo_time::Uptime::Now() + roo_time::Millis(500);
    while (!callback_done) {
      if (callback_done_cv.wait_until(guard, deadline) ==
          roo::cv_status::timeout) {
        _exit(22);
      }
    }
  }

  if (reconnected == nullptr) {
    return 23;
  }
  if (!reconnected->awaitConnected(roo_time::Millis(500)) ||
      reconnected->status() != LinkStatus::kConnected) {
    return 24;
  }
  return 0;
}

int RunTransportScenario(const char* scenario_name) {
  if (strcmp(scenario_name, "quiescent_small_send_buffer") == 0) {
    return RunQuiescentSmallSendBufferScenario();
  }
  if (strcmp(scenario_name, "disconnect_callback_reenter") == 0) {
    return RunDisconnectCallbackReenterScenario();
  }
  return 99;
}
#endif

}  // namespace

class NullPacketSender : public PacketSender {
 public:
  void send(const roo::byte* buf, size_t len) override {}
};

class NullPacketReceiver : public PacketReceiver {
 public:
  size_t tryReceive(const ReceiverFn& receiver_fn) override { return 0; }
  size_t receive(const ReceiverFn& receiver_fn) override { return 0; }
};

TEST(LinkTransport, DefaultConstructedLinkIsIdle) {
  Link link;
  EXPECT_EQ(link.status(), LinkStatus::kIdle);
  EXPECT_EQ(link.in().status(), roo_io::kClosed);
  EXPECT_EQ(link.out().status(), roo_io::kClosed);
}

TEST(LinkTransport, TransportConstructedLinkIsConnecting) {
  NullPacketSender sender;
  NullPacketReceiver receiver;
  LinkTransport transport(sender);
  Link link = transport.connectAsync();
  EXPECT_EQ(link.status(), LinkStatus::kConnecting);
  EXPECT_EQ(link.in().status(), roo_io::kOk);
  EXPECT_EQ(link.out().status(), roo_io::kOk);
}

// Testing the happy path.
TEST(LinkTransport, SimpleConnectSendDisconnect) {
  LinkLoopback loopback;

  Link server = loopback.server().connectAsync();
  EXPECT_EQ(server.status(), LinkStatus::kConnecting);
  EXPECT_EQ(server.in().status(), roo_io::kOk);
  EXPECT_EQ(server.out().status(), roo_io::kOk);
  Link client = loopback.client().connect();
  EXPECT_EQ(client.status(), LinkStatus::kConnected);
  EXPECT_EQ(client.in().status(), roo_io::kOk);
  EXPECT_EQ(client.out().status(), roo_io::kOk);
  server.awaitConnected();
  EXPECT_EQ(server.status(), LinkStatus::kConnected);
  EXPECT_EQ(server.in().status(), roo_io::kOk);
  client.out().writeFully((const roo::byte*)"Request", 8);
  client.out().close();
  EXPECT_EQ(client.out().status(), roo_io::kClosed);
  roo::byte buf[10];
  size_t n = server.in().readFully(buf, 10);
  EXPECT_EQ(n, size_t{8});
  EXPECT_EQ(memcmp(buf, "Request", 8), 0);
  EXPECT_EQ(server.in().status(), roo_io::kEndOfStream);
  server.out().writeFully((const roo::byte*)"Response", 9);
  server.out().close();
  EXPECT_EQ(server.out().status(), roo_io::kClosed);
  n = client.in().readFully(buf, 10);
  EXPECT_EQ(n, size_t{9});
  EXPECT_EQ(memcmp(buf, "Response", 9), 0);
  EXPECT_EQ(client.in().status(), roo_io::kEndOfStream);

  client.disconnect();
  EXPECT_EQ(client.status(), LinkStatus::kIdle);
  server.disconnect();
  EXPECT_EQ(server.status(), LinkStatus::kIdle);
}

TEST(LinkTransport, SyncConnect) {
  LinkLoopback loopback;

  roo::thread server_thread([&]() {
    Link server = loopback.server().connect();
    EXPECT_EQ(server.status(), LinkStatus::kConnected);
    EXPECT_EQ(server.in().status(), roo_io::kOk);
    EXPECT_EQ(server.out().status(), roo_io::kOk);
    roo::byte buf[10];
    size_t n = server.in().readFully(buf, 10);
    EXPECT_EQ(n, size_t{8});
    EXPECT_EQ(memcmp(buf, "Request", 8), 0);
    EXPECT_EQ(server.in().status(), roo_io::kEndOfStream);
    server.out().writeFully((const roo::byte*)"Response", 9);
    server.out().close();
    EXPECT_EQ(server.out().status(), roo_io::kClosed);
  });
  Link client = loopback.client().connect();
  EXPECT_EQ(client.status(), LinkStatus::kConnected);
  EXPECT_EQ(client.in().status(), roo_io::kOk);
  EXPECT_EQ(client.out().status(), roo_io::kOk);
  client.out().writeFully((const roo::byte*)"Request", 8);
  client.out().close();
  EXPECT_EQ(client.out().status(), roo_io::kClosed);
  roo::byte buf[10];
  size_t n = client.in().readFully(buf, 10);
  EXPECT_EQ(n, size_t{9});
  EXPECT_EQ(memcmp(buf, "Response", 9), 0);
  EXPECT_EQ(client.in().status(), roo_io::kEndOfStream);

  server_thread.join();
}

TEST(LinkTransport, SyncConnectReconnect) {
  LinkLoopback loopback;

  roo::thread server_thread([&]() {
    Link server_throwaway = loopback.server().connect();
    // Note: by the time we're checking, might already be broken by the
    // subsequent client reconnect.
    EXPECT_TRUE(server_throwaway.status() == LinkStatus::kConnected ||
                server_throwaway.status() == LinkStatus::kBroken)
        << (int)server_throwaway.status();
    // Reconnect.
    Link server = loopback.server().connect();
    EXPECT_EQ(server_throwaway.status(), LinkStatus::kBroken);
    EXPECT_EQ(server.status(), LinkStatus::kConnected);
    EXPECT_EQ(server.in().status(), roo_io::kOk);
    EXPECT_EQ(server.out().status(), roo_io::kOk);
    roo::byte buf[10];
    size_t n = server.in().readFully(buf, 10);
    EXPECT_EQ(n, size_t{8});
    EXPECT_EQ(memcmp(buf, "Request", 8), 0);
    EXPECT_EQ(server.in().status(), roo_io::kEndOfStream);
    server.out().writeFully((const roo::byte*)"Response", 9);
    server.out().close();
    EXPECT_EQ(server.out().status(), roo_io::kClosed);
  });
  Link client_throwaway = loopback.client().connect();
  // Note: by the time we're checking, might already be broken by the
  // subsequent server reconnect.
  EXPECT_TRUE(client_throwaway.status() == LinkStatus::kConnected ||
              client_throwaway.status() == LinkStatus::kBroken)
      << (int)client_throwaway.status();
  // Reconnect.
  Link client = loopback.client().connect();
  EXPECT_EQ(client_throwaway.status(), LinkStatus::kBroken);
  EXPECT_EQ(client.status(), LinkStatus::kConnected);
  EXPECT_EQ(client.in().status(), roo_io::kOk);
  EXPECT_EQ(client.out().status(), roo_io::kOk);
  client.out().writeFully((const roo::byte*)"Request", 8);
  client.out().close();
  EXPECT_EQ(client.out().status(), roo_io::kClosed);
  roo::byte buf[10];
  size_t n = client.in().readFully(buf, 10);
  EXPECT_EQ(n, size_t{9});
  EXPECT_EQ(memcmp(buf, "Response", 9), 0);
  EXPECT_EQ(client.in().status(), roo_io::kEndOfStream);

  server_thread.join();
}

TEST(LinkTransport, SyncConnectCommReconnect) {
  LinkLoopback loopback;

  roo::thread server_thread([&]() {
    Link server = loopback.server().connect();
    EXPECT_EQ(server.status(), LinkStatus::kConnected);
    EXPECT_EQ(server.in().status(), roo_io::kOk);
    EXPECT_EQ(server.out().status(), roo_io::kOk);
    roo::byte buf[10];
    size_t n = server.in().readFully(buf, 10);
    EXPECT_EQ(n, size_t{8});
    EXPECT_EQ(memcmp(buf, "Request", 8), 0);
    EXPECT_EQ(server.in().status(), roo_io::kEndOfStream);
    server.out().writeFully((const roo::byte*)"Response", 9);
    server.out().close();
    EXPECT_EQ(server.out().status(), roo_io::kClosed);
    delay(1000);
    server = loopback.server().connect();
    EXPECT_EQ(server.status(), LinkStatus::kConnected);
  });
  Link client = loopback.client().connect();
  EXPECT_EQ(client.status(), LinkStatus::kConnected);
  EXPECT_EQ(client.in().status(), roo_io::kOk);
  EXPECT_EQ(client.out().status(), roo_io::kOk);
  client.out().writeFully((const roo::byte*)"Request", 8);
  client.out().close();
  EXPECT_EQ(client.out().status(), roo_io::kClosed);
  roo::byte buf[10];
  size_t n = client.in().readFully(buf, 10);
  EXPECT_EQ(n, size_t{9});
  EXPECT_EQ(memcmp(buf, "Response", 9), 0);
  EXPECT_EQ(client.in().status(), roo_io::kEndOfStream);
  client = loopback.client().connect();
  EXPECT_EQ(client.status(), LinkStatus::kConnected);

  server_thread.join();
}

TEST(LinkTransport, ThrashingReconnect) {
  LinkLoopback loopback;
  const size_t kIterations = 100;

  roo::thread server_thread([&]() {
    Link server;
    for (size_t i = 0; i < kIterations; i++) {
      server = loopback.server().connect();
      // Note: by the time we're checking, might already be broken by the
      // subsequent client reconnect.
      EXPECT_TRUE(server.status() == LinkStatus::kConnected ||
                  server.status() == LinkStatus::kBroken)
          << (int)server.status();
      EXPECT_EQ(server.in().status(), roo_io::kOk);
      EXPECT_EQ(server.out().status(), roo_io::kOk);
    }
    server.out().close();
    EXPECT_EQ(server.out().status(), roo_io::kClosed);
  });
  Link client;
  for (size_t i = 0; i < kIterations; i++) {
    client = loopback.client().connect();
    // Note: by the time we're checking, might already be broken by the
    // subsequent client reconnect.
    EXPECT_TRUE(client.status() == LinkStatus::kConnected ||
                client.status() == LinkStatus::kBroken)
        << (int)client.status();
    EXPECT_EQ(client.in().status(), roo_io::kOk);
    EXPECT_EQ(client.out().status(), roo_io::kOk);
  }
  client.out().close();
  EXPECT_EQ(client.out().status(), roo_io::kClosed);

  server_thread.join();
}

TEST(LinkTransport, DisconnectFnCalledWhenDisconnectDetected) {
  LinkLoopback loopback;

  roo::thread server_thread([&]() {
    Link server = loopback.server().connect();
    // Note: by the time we're checking, might already be broken by the
    // subsequent client reconnect.
    EXPECT_EQ(server.status(), LinkStatus::kConnected);
    // Reconnect -> should trigger disconnection fn on the client.
    server = loopback.server().connect();
    EXPECT_EQ(server.status(), LinkStatus::kConnected);
    server.out().close();
  });
  int disconnect_counter = 0;
  Link client = loopback.client().connect([&]() { disconnect_counter++; });
  client.out().close();
  EXPECT_EQ(disconnect_counter, 1);
  EXPECT_EQ(client.status(), LinkStatus::kBroken);
  client = loopback.client().connect();
  EXPECT_EQ(disconnect_counter, 1);
  EXPECT_EQ(client.status(), LinkStatus::kConnected);
  client.out().close();

  server_thread.join();
}

#if defined(__unix__)
TEST(LinkTransportSubprocess, RunScenario) {
  const char* scenario_name = getenv(kTransportScenarioEnv);
  if (scenario_name == nullptr) {
    GTEST_SKIP();
  }
  ASSERT_EQ(RunTransportScenario(scenario_name), 0);
}

TEST(LinkTransport, QuiescentSmallSendBufferWriteCompletesPromptly) {
  ExpectSubprocessSuccess("quiescent_small_send_buffer");
}

TEST(LinkTransport, DisconnectCallbackMayReenterTransport) {
  ExpectSubprocessSuccess("disconnect_callback_reenter");
}
#endif

class TransferTest : public ::testing::TestWithParam<int> {
 protected:
  TransferTest() : loopback_() {}

  ~TransferTest() { join(); }

  void setServerOutputErrorRate(int error_rate) {
    loopback_.setServerOutputErrorRate(error_rate);
  }

  void setClientOutputErrorRate(int error_rate) {
    loopback_.setClientOutputErrorRate(error_rate);
  }

  void join() {
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    if (client_thread_.joinable()) {
      client_thread_.join();
    }
  }

  void server(
      std::function<void(roo_io::InputStream& in, roo_io::OutputStream& out)>
          fn) {
    roo::thread::attributes server_attrs;
    server_attrs.set_name("server");
    server_thread_ = roo::thread(server_attrs, [this, fn]() {
      Link server = loopback_.server().connect();
      ASSERT_EQ(server.status(), LinkStatus::kConnected);
      ASSERT_EQ(server.in().status(), roo_io::kOk);
      ASSERT_EQ(server.out().status(), roo_io::kOk);
      fn(server.in(), server.out());
      server.out().close();
      ASSERT_EQ(server.out().status(), roo_io::kClosed);
    });
  }

  void client(
      std::function<void(roo_io::InputStream& in, roo_io::OutputStream& out)>
          fn) {
    roo::thread::attributes client_attrs;
    client_attrs.set_name("client");
    client_thread_ = roo::thread(client_attrs, [this, fn]() {
      Link client = loopback_.client().connect();
      ASSERT_EQ(client.status(), LinkStatus::kConnected);
      ASSERT_EQ(client.in().status(), roo_io::kOk);
      ASSERT_EQ(client.out().status(), roo_io::kOk);
      fn(client.in(), client.out());
      client.out().close();
      ASSERT_EQ(client.out().status(), roo_io::kClosed);
    });
  }

  LinkLoopback loopback_;
  roo::thread server_thread_;
  roo::thread client_thread_;
};

std::unique_ptr<roo::byte[]> make_large_buffer(size_t size) {
  std::unique_ptr<roo::byte[]> buf(new roo::byte[size]);
  for (size_t i = 0; i < size; i++) {
    buf[i] = roo::byte(rand() % 256);
  }
  return buf;
}

TEST_P(TransferTest, LargeRequestResponse) {
  int error_rate = GetParam();
  if (error_rate > 0) {
    setServerOutputErrorRate(error_rate);
    setClientOutputErrorRate(error_rate);
  }

  const size_t kRequestSize = 200000;
  const size_t kResponseSize = 500000;
  auto request = make_large_buffer(kRequestSize);
  auto response = make_large_buffer(kResponseSize);

  server([&](roo_io::InputStream& in, roo_io::OutputStream& out) {
    size_t request_byte_idx = 0;
    while (request_byte_idx < kRequestSize) {
      EXPECT_EQ(in.status(), roo_io::kOk);
      roo::byte buf[1000];
      size_t count = rand() % 1000 + 1;
      size_t n = in.read(buf, count);
      ASSERT_GT(n, size_t{0});
      for (size_t i = 0; i < n; i++) {
        EXPECT_EQ(buf[i], request[request_byte_idx + i]);
      }
      request_byte_idx += n;
    }
    EXPECT_EQ(in.status(), roo_io::kOk);
    EXPECT_EQ(out.status(), roo_io::kOk);
    size_t response_byte_idx = 0;
    while (response_byte_idx < kResponseSize) {
      EXPECT_EQ(out.status(), roo_io::kOk);
      size_t count = rand() % 1000 + 1;
      if (count > kResponseSize - response_byte_idx) {
        count = kResponseSize - response_byte_idx;
      }
      size_t n = out.write(&response[response_byte_idx], count);
      ASSERT_GT(n, size_t{0});
      response_byte_idx += n;
    }
  });

  client([&](roo_io::InputStream& in, roo_io::OutputStream& out) {
    size_t request_byte_idx = 0;
    while (request_byte_idx < kRequestSize) {
      size_t count = rand() % 1000 + 1;
      if (count > kRequestSize - request_byte_idx) {
        count = kRequestSize - request_byte_idx;
      }
      size_t n = out.write(&request[request_byte_idx], count);
      ASSERT_GT(n, size_t{0});
      request_byte_idx += n;
    }
    out.close();
    EXPECT_EQ(out.status(), roo_io::kClosed);
    roo::byte buf[1000];
    size_t response_byte_idx = 0;
    while (response_byte_idx < kResponseSize) {
      EXPECT_EQ(in.status(), roo_io::kOk);
      size_t count = rand() % 1000 + 1;
      size_t n = in.read(buf, count);
      ASSERT_GT(n, size_t{0});
      for (size_t i = 0; i < n; i++) {
        EXPECT_EQ(buf[i], response[response_byte_idx + i]);
      }
      response_byte_idx += n;
    }
    EXPECT_EQ(in.read(buf, 1), size_t{0});
    EXPECT_EQ(in.status(), roo_io::kEndOfStream);
  });

  join();
}

TEST_P(TransferTest, BidiStreaming) {
  int error_rate = GetParam();
  if (error_rate > 0) {
    setServerOutputErrorRate(error_rate);
    setClientOutputErrorRate(error_rate);
  }

  const size_t kRequestSize = 200000;
  const size_t kResponseSize = 200000;
  auto request = make_large_buffer(kRequestSize);
  auto response = make_large_buffer(kResponseSize);

  server([&](roo_io::InputStream& in, roo_io::OutputStream& out) {
    roo::thread server_recv([&]() {
      roo::byte buf[1000];
      size_t request_byte_idx = 0;
      while (request_byte_idx < kRequestSize) {
        EXPECT_EQ(in.status(), roo_io::kOk);
        size_t count = rand() % 1000 + 1;
        size_t n = in.read(buf, count);
        ASSERT_GT(n, size_t{0});
        for (size_t i = 0; i < n; i++) {
          EXPECT_EQ(buf[i], request[request_byte_idx + i]);
        }
        request_byte_idx += n;
      }
      EXPECT_EQ(in.read(buf, 1), size_t{0});
      EXPECT_EQ(in.status(), roo_io::kEndOfStream);
    });
    size_t response_byte_idx = 0;
    while (response_byte_idx < kResponseSize) {
      EXPECT_EQ(out.status(), roo_io::kOk);
      size_t count = rand() % 1000 + 1;
      if (count > kResponseSize - response_byte_idx) {
        count = kResponseSize - response_byte_idx;
      }
      size_t n = out.write(&response[response_byte_idx], count);
      ASSERT_GT(n, size_t{0});
      response_byte_idx += n;
    }
    out.close();
    EXPECT_EQ(out.status(), roo_io::kClosed);

    server_recv.join();
  });

  client([&](roo_io::InputStream& in, roo_io::OutputStream& out) {
    roo::thread client_recv([&]() {
      roo::byte buf[1000];
      size_t response_byte_idx = 0;
      while (response_byte_idx < kResponseSize) {
        EXPECT_EQ(in.status(), roo_io::kOk);
        size_t count = rand() % 1000 + 1;
        size_t n = in.read(buf, count);
        ASSERT_GT(n, size_t{0});
        for (size_t i = 0; i < n; i++) {
          EXPECT_EQ(buf[i], response[response_byte_idx + i]);
        }
        response_byte_idx += n;
      }
      EXPECT_EQ(in.read(buf, 1), size_t{0});
      EXPECT_EQ(in.status(), roo_io::kEndOfStream);
    });

    size_t request_byte_idx = 0;
    while (request_byte_idx < kRequestSize) {
      size_t count = rand() % 1000 + 1;
      if (count > kRequestSize - request_byte_idx) {
        count = kRequestSize - request_byte_idx;
      }
      size_t n = out.write(&request[request_byte_idx], count);
      ASSERT_GT(n, size_t{0});
      request_byte_idx += n;
    }
    out.close();
    EXPECT_EQ(out.status(), roo_io::kClosed);

    client_recv.join();
  });

  join();
}

INSTANTIATE_TEST_SUITE_P(TransferTests, TransferTest,
                         ::testing::Values(0, 1, 2, 10)  // Error rates to test.
);

}  // namespace roo_transport