#pragma once

#include <Arduino.h>

#include "../data/SensorData.h"

template <size_t N>
class RecordBuffer {
 public:
  size_t size() const { return count_; }
  size_t capacity() const { return N; }
  bool empty() const { return count_ == 0; }
  uint32_t dropped() const { return dropped_; }

  void push(const SensorData& d) {
    items_[head_] = d;
    head_ = (head_ + 1) % N;
    if (count_ == N) {
      tail_ = (tail_ + 1) % N;
      dropped_++;
    } else {
      count_++;
    }
  }

  bool peek(SensorData& out) const {
    if (count_ == 0) return false;
    out = items_[tail_];
    return true;
  }

  void pop() {
    if (count_ == 0) return;
    tail_ = (tail_ + 1) % N;
    count_--;
  }

 private:
  SensorData items_[N];
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
  uint32_t dropped_ = 0;
};
