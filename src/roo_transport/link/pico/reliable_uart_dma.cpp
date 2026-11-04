#include "roo_transport/link/pico/reliable_uart_dma.h"

#if defined(ARDUINO_ARCH_RP2040) && defined(__FREERTOS) && !defined(ROO_TESTING)

#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/sync.h>
#include <pico/platform.h>

#include <algorithm>
#include <cstring>

#include "roo_threads/atomic.h"
#include "roo_threads/thread.h"
#include "roo_transport/packets/over_stream/packet_receiver_over_stream.h"
#include "roo_transport/packets/over_stream/packet_sender_over_stream.h"

namespace roo_transport {
namespace pico {

struct ReliableUartDma::Impl {
  static constexpr size_t kRingSize = ReliableUartDma::kRxBufferSize;
  static constexpr size_t kRingMask = kRingSize - 1;
#ifdef ROO_TRANSPORT_PICO_DMA_TEST_TRANSFER_COUNT
  static constexpr uint32_t kRxTransferCount =
      ROO_TRANSPORT_PICO_DMA_TEST_TRANSFER_COUNT;
#else
  static constexpr uint32_t kRxTransferCount = UINT32_MAX;
#endif

  // These streams have one consumer/producer each. The packet/link layer owns
  // application synchronization; the interrupt only advances the TX tail.
  class Input : public roo_io::InputStream {
   public:
    explicit Input(Impl& impl) : impl_(impl) {}

    size_t tryRead(roo::byte* bytes, size_t count) override {
      return impl_.readAvailable(bytes, count);
    }

    size_t read(roo::byte* bytes, size_t count) override {
      if (count == 0) return 0;
      size_t result;
      while ((result = tryRead(bytes, count)) == 0) {
        roo::this_thread::yield();
      }
      return result;
    }

    roo_io::Status status() const override { return roo_io::kOk; }

   private:
    Impl& impl_;
  };

  class Output : public roo_io::OutputStream {
   public:
    explicit Output(Impl& impl) : impl_(impl) {}

    size_t write(const roo::byte* bytes, size_t count) override {
      return impl_.write(bytes, count);
    }

    void flush() override { impl_.flush(); }

    roo_io::Status status() const override { return roo_io::kOk; }

   private:
    Impl& impl_;
  };

  Impl(uart_inst_t* uart, uint8_t* rx_buffer, LinkBufferSize sendbuf,
       LinkBufferSize recvbuf)
      : uart(uart),
        rx_buffer(rx_buffer),
        tx_buffer(new uint8_t[kRingSize]),
        input(*this),
        output(*this),
        sender(output),
        receiver(input),
        link(sender, sendbuf, recvbuf) {
    CHECK(uart == uart0 || uart == uart1);
    CHECK(rx_buffer != nullptr);
    CHECK_EQ(reinterpret_cast<uintptr_t>(rx_buffer) & kRingMask, 0u);
    CHECK(instances[uart_get_index(uart)] == nullptr);
    irq_core = get_core_num();
    for (Impl* instance : instances) {
      if (instance != nullptr) CHECK_EQ(instance->irq_core, irq_core);
    }
    rx_channel = dma_claim_unused_channel(true);
    tx_channel = dma_claim_unused_channel(true);
    spinlock_number = spin_lock_claim_unused(true);
    tx_lock = spin_lock_instance(spinlock_number);
    uart_set_irq_enables(uart, false, false);
    hw_set_bits(&uart_get_hw(uart)->dmacr,
                UART_UARTDMACR_RXDMAE_BITS | UART_UARTDMACR_TXDMAE_BITS);

    dma_channel_config rx = dma_channel_get_default_config(rx_channel);
    channel_config_set_transfer_data_size(&rx, DMA_SIZE_8);
    channel_config_set_read_increment(&rx, false);
    channel_config_set_write_increment(&rx, true);
    channel_config_set_ring(&rx, true, 13);
    channel_config_set_dreq(&rx, uart_get_dreq(uart, false));
    dma_channel_configure(rx_channel, &rx, rx_buffer, &uart_get_hw(uart)->dr,
                          kRxTransferCount, true);

    dma_channel_config tx = dma_channel_get_default_config(tx_channel);
    channel_config_set_transfer_data_size(&tx, DMA_SIZE_8);
    channel_config_set_read_increment(&tx, true);
    channel_config_set_write_increment(&tx, false);
    channel_config_set_dreq(&tx, uart_get_dreq(uart, true));
    dma_channel_configure(tx_channel, &tx, &uart_get_hw(uart)->dr,
                          tx_buffer.get(), 0, false);
    if (instances[0] == nullptr && instances[1] == nullptr) {
      irq_add_shared_handler(DMA_IRQ_0, TxIrq,
                             PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    }
    instances[uart_get_index(uart)] = this;
    dma_hw->ints0 = 1u << tx_channel;
    dma_channel_set_irq0_enabled(tx_channel, true);
    irq_set_enabled(DMA_IRQ_0, true);
  }

  ~Impl() { end(); }

  void begin() {
    CHECK(!started && !closed);
    started = true;
    running = true;
    link.begin();
    roo::thread::attributes attrs;
    attrs.set_name("dma-recv");
    attrs.set_priority(configMAX_PRIORITIES - 1);
    receive_thread = roo::thread(attrs, [this] { receiveLoop(); });
  }

  void end() {
    if (closed) return;
    CHECK_EQ(get_core_num(), irq_core);
    if (started) {
      link.end();
      running = false;
      receive_thread.join();
    }
    flush();
    const uint32_t saved = save_and_disable_interrupts();
    dma_channel_set_irq0_enabled(tx_channel, false);
    dma_channel_abort(tx_channel);
    dma_channel_abort(rx_channel);
    dma_hw->ints0 = 1u << tx_channel;
    instances[uart_get_index(uart)] = nullptr;
    if (instances[0] == nullptr && instances[1] == nullptr) {
      irq_remove_handler(DMA_IRQ_0, TxIrq);
    }
    restore_interrupts(saved);
    dma_channel_unclaim(tx_channel);
    dma_channel_unclaim(rx_channel);
    spin_lock_unclaim(spinlock_number);
    closed = true;
  }

  // Renew the finite DMA count before it reaches zero, preserving the write
  // address and cumulative producer position. IRQs stay masked across abort and
  // restart so the task cannot be preempted/migrated while DMA is paused. The
  // UART FIFO absorbs arrivals during this short pause (roughly once per 90
  // minutes at 8 Mbaud with the normal UINT32_MAX count).
  uint32_t produced() {
    uint32_t remaining = dma_channel_hw_addr(rx_channel)->transfer_count;
    if (remaining < kRingSize) {
      const uint32_t saved = save_and_disable_interrupts();
      // RP2040 abort clears TRANS_COUNT. Sample it before abort, then resolve
      // the committed write address against that estimate, including a ring
      // wrap. IRQ masking bounds the interval to much less than half a ring;
      // a signed offset also handles an in-flight write lagging the count.
      const uint32_t estimate = rx_base + kRxTransferCount -
                                dma_channel_hw_addr(rx_channel)->transfer_count;
      dma_channel_abort(rx_channel);
      const uint32_t offset = (dma_channel_hw_addr(rx_channel)->write_addr -
                               reinterpret_cast<uintptr_t>(rx_buffer)) &
                              kRingMask;
      int32_t advance = static_cast<int32_t>((offset - estimate) & kRingMask);
      if (advance > static_cast<int32_t>(kRingSize / 2)) {
        advance -= static_cast<int32_t>(kRingSize);
      }
      rx_base = estimate + advance;
      dma_channel_set_trans_count(rx_channel, kRxTransferCount, true);
      restore_interrupts(saved);
      remaining = dma_channel_hw_addr(rx_channel)->transfer_count;
    }
    return rx_base + kRxTransferCount - remaining;
  }

  size_t readAvailable(roo::byte* bytes, size_t count) {
    if (count == 0) return 0;
    uint32_t pending = produced() - rx_consumed;
    if (pending > kRingSize) {
      ++overruns;
      rx_consumed += pending - kRingSize;
      pending = kRingSize;
    }
    count = std::min<size_t>(count, pending);
    const size_t offset = rx_consumed & kRingMask;
    const size_t first = std::min(count, kRingSize - offset);
    __dmb();
    memcpy(bytes, rx_buffer + offset, first);
    memcpy(bytes + first, rx_buffer, count - first);
    rx_consumed += count;
    const uint32_t errors = uart_get_hw(uart)->rsr;
    if (errors != 0) {
      ++receive_errors;
      uart_get_hw(uart)->rsr = 0;
    }
    return count;
  }

  size_t write(const roo::byte* bytes, size_t count) {
    const size_t total = count;
    while (count > 0) {
      uint32_t saved = spin_lock_blocking(tx_lock);
      const size_t free = kRingSize - (tx_head - tx_tail);
      spin_unlock(tx_lock, saved);
      if (free == 0) {
        roo::this_thread::yield();
        continue;
      }
      const size_t offset = tx_head & kRingMask;
      const size_t chunk = std::min(count, std::min(free, kRingSize - offset));
      memcpy(tx_buffer.get() + offset, bytes, chunk);
      saved = spin_lock_blocking(tx_lock);
      tx_head += chunk;
      if (tx_inflight == 0) startTx();
      spin_unlock(tx_lock, saved);
      bytes += chunk;
      count -= chunk;
    }
    return total;
  }

  void flush() {
    if (closed) return;
    while (true) {
      const uint32_t saved = spin_lock_blocking(tx_lock);
      const bool done = tx_head == tx_tail;
      spin_unlock(tx_lock, saved);
      if (done) break;
      roo::this_thread::yield();
    }
    uart_tx_wait_blocking(uart);
  }

  // Called with tx_lock held, after publishing bytes or completing a segment.
  void startTx() {
    tx_inflight = std::min<uint32_t>(tx_head - tx_tail,
                                     kRingSize - (tx_tail & kRingMask));
    if (tx_inflight != 0) {
      dma_channel_transfer_from_buffer_now(
          tx_channel, tx_buffer.get() + (tx_tail & kRingMask), tx_inflight);
    }
  }

  static void TxIrq() {
    for (Impl* instance : instances) {
      if (instance == nullptr ||
          (dma_hw->ints0 & (1u << instance->tx_channel)) == 0) {
        continue;
      }
      dma_hw->ints0 = 1u << instance->tx_channel;
      const uint32_t saved = spin_lock_blocking(instance->tx_lock);
      instance->tx_tail += instance->tx_inflight;
      instance->tx_inflight = 0;
      instance->startTx();
      spin_unlock(instance->tx_lock, saved);
    }
  }

  void receiveLoop() {
    while (running) {
      if (produced() == rx_consumed) {
        vTaskDelay(1);
      } else {
        receiver.tryReceive([this](const roo::byte* bytes, size_t count) {
          link.processIncomingPacket(bytes, count);
        });
      }
    }
  }

  static Impl* instances[2];
  uart_inst_t* uart;
  uint8_t* rx_buffer;
  std::unique_ptr<uint8_t[]> tx_buffer;
  unsigned int irq_core;
  int rx_channel;
  int tx_channel;
  int spinlock_number;
  spin_lock_t* tx_lock;
  uint32_t rx_base = 0;
  uint32_t rx_consumed = 0;
  uint32_t tx_head = 0;
  uint32_t tx_tail = 0;
  uint32_t tx_inflight = 0;
  roo::atomic<uint32_t> overruns{0};
  roo::atomic<uint32_t> receive_errors{0};
  Input input;
  Output output;
  PacketSenderOverStream sender;
  PacketReceiverOverStream receiver;
  LinkTransport link;
  roo::thread receive_thread;
  roo::atomic<bool> running{false};
  bool started = false;
  bool closed = false;
};

ReliableUartDma::Impl* ReliableUartDma::Impl::instances[2] = {};

ReliableUartDma::ReliableUartDma(uart_inst_t* uart, uint8_t* rx_buffer,
                                 LinkBufferSize sendbuf, LinkBufferSize recvbuf)
    : impl_(new Impl(uart, rx_buffer, sendbuf, recvbuf)) {}

ReliableUartDma::~ReliableUartDma() = default;

void ReliableUartDma::begin() { impl_->begin(); }

void ReliableUartDma::end() { impl_->end(); }

LinkTransport& ReliableUartDma::transport() { return impl_->link; }

uint32_t ReliableUartDma::receiveOverruns() const { return impl_->overruns; }

uint32_t ReliableUartDma::receiveErrors() const {
  return impl_->receive_errors;
}

}  // namespace pico
}  // namespace roo_transport
#endif
