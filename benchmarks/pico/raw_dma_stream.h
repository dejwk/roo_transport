#pragma once
#include <Arduino.h>
#include <hardware/dma.h>
#include <hardware/uart.h>

#include <algorithm>
#include <cstring>

// Diagnostic UART backend: no Arduino UART mutexes; one RX consumer, one TX
// producer, and a DMA ring whose lifetime spans the complete benchmark run.
class BenchDmaStream : public Stream {
 public:
  BenchDmaStream(uart_inst_t* uart, uint8_t* ring) : uart_(uart), ring_(ring) {
    channel_ = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(channel_);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_ring(&cfg, true, 13);
    channel_config_set_dreq(&cfg, uart_get_dreq(uart_, false));
    dma_channel_configure(channel_, &cfg, ring_, &uart_get_hw(uart_)->dr,
                          UINT32_MAX, true);
  }

  int available() override {
    const uint32_t produced =
        UINT32_MAX - dma_channel_hw_addr(channel_)->transfer_count;
    const uint32_t pending = produced - consumed_;
    if (pending > 8192) {
      ++overruns;
      consumed_ = produced - 8192;
      return 8192;
    }
    return pending;
  }

  size_t readAvailable(uint8_t* bytes, size_t count) {
    count = std::min<size_t>(count, available());
    const size_t offset = consumed_ & 8191;
    const size_t first = std::min(count, 8192 - offset);
    __dmb();
    memcpy(bytes, ring_ + offset, first);
    memcpy(bytes + first, ring_, count - first);
    consumed_ += count;
    return count;
  }

  int read() override {
    if (available() == 0) return -1;
    __dmb();
    return ring_[consumed_++ & 8191];
  }

  int peek() override {
    if (available() == 0) return -1;
    __dmb();
    return ring_[consumed_ & 8191];
  }

  size_t write(uint8_t value) override { return write(&value, 1); }

  size_t write(const uint8_t* bytes, size_t count) override {
    uart_write_blocking(uart_, bytes, count);
    return count;
  }

  int availableForWrite() override { return uart_is_writable(uart_) ? 1 : 0; }
  void flush() override { uart_tx_wait_blocking(uart_); }
  uint32_t overruns = 0;

 private:
  uart_inst_t* uart_;
  uint8_t* ring_;
  uint32_t consumed_ = 0;
  int channel_;
};
