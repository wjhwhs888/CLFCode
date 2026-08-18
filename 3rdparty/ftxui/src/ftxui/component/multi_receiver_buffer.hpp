// Copyright 2025 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
//
// CLFCode patch: MultiReceiverBuffer was not thread-safe — App::PostEvent()
// pushes from worker threads (CLFCode timer/submit threads) while the UI loop
// pops/prunes on the main thread, racing on the same std::deque. All member
// accesses are now guarded by a recursive_mutex (recursive because Pop() ->
// Get()/Prune() re-enter). Re-check this file when upgrading FTXUI.
#ifndef FTXUI_COMPONENT_MULTI_RECEIVER_BUFFER_HPP
#define FTXUI_COMPONENT_MULTI_RECEIVER_BUFFER_HPP

#include <algorithm>  // for std::replace, std::min, std::remove
#include <deque>      // for deque
#include <memory>     // for unique_ptr, make_unique
#include <mutex>      // for recursive_mutex
#include <vector>     // for vector

namespace ftxui {

template <typename T>
class MultiReceiverBuffer {
 public:
  class Receiver {
   public:
    explicit Receiver(MultiReceiverBuffer* buffer) : buffer_(buffer), index_(0) {
      std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);
      index_ = buffer->next_index_;
      buffer_->receivers_.push_back(this);
    }

    Receiver(MultiReceiverBuffer* buffer, size_t index)
        : buffer_(buffer), index_(index) {
      std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);
      buffer_->receivers_.push_back(this);
    }

    ~Receiver() {
      if (buffer_) {
        buffer_->RemoveReceiver(this);
      }
    }

    Receiver(const Receiver&) = delete;
    Receiver(Receiver&& other) noexcept
        : buffer_(other.buffer_), index_(other.index_) {
      other.buffer_ = nullptr;
      if (buffer_) {
        std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);
        std::replace(buffer_->receivers_.begin(), buffer_->receivers_.end(),
                     &other, this);
      }
    }

    Receiver& operator=(const Receiver&) = delete;
    Receiver& operator=(Receiver&& other) noexcept {
      if (this != &other) {
        if (buffer_) {
          buffer_->RemoveReceiver(this);
        }
        buffer_ = other.buffer_;
        index_ = other.index_;
        other.buffer_ = nullptr;
        if (buffer_) {
          std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);
          std::replace(buffer_->receivers_.begin(), buffer_->receivers_.end(),
                       &other, this);
        }
      }
      return *this;
    }

    bool Has() const {
      if (!buffer_) {
        return false;
      }
      std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);
      return index_ < buffer_->next_index_;
    }

    T Pop() {
      if (!buffer_) {
        return {};
      }
      std::lock_guard<std::recursive_mutex> lock(buffer_->mutex_);
      if (!Has()) {
        return {};
      }
      T value = buffer_->Get(index_);
      index_++;
      buffer_->Prune();
      return value;
    }

    size_t index() const { return index_; }

   private:
    friend class MultiReceiverBuffer;
    MultiReceiverBuffer* buffer_;
    size_t index_;
  };

  std::unique_ptr<Receiver> CreateReceiver() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return std::make_unique<Receiver>(this);
  }

  std::unique_ptr<Receiver> CreateReceiverAt(size_t index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return std::make_unique<Receiver>(this, index);
  }

  void Push(T value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    values_.push_back(std::move(value));
    next_index_++;
  }

 private:
  void RemoveReceiver(Receiver* receiver) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    receivers_.erase(
        std::remove(receivers_.begin(), receivers_.end(), receiver),
        receivers_.end());
    Prune();
  }

  void Prune() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (receivers_.empty()) {
      values_.clear();
      start_index_ = next_index_;
      return;
    }
    size_t min_index = next_index_;
    for (auto* r : receivers_) {
      min_index = std::min(min_index, r->index_);
    }
    while (start_index_ < min_index) {
      values_.pop_front();
      start_index_++;
    }
  }

  T Get(size_t index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return values_[index - start_index_];
  }

  mutable std::recursive_mutex mutex_;

  std::deque<T> values_;
  std::vector<Receiver*> receivers_;
  size_t start_index_ = 0;
  size_t next_index_ = 0;
};

}  // namespace ftxui

#endif  // FTXUI_COMPONENT_MULTI_RECEIVER_BUFFER_HPP
