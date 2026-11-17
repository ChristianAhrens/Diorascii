/// @file core/seqlock.h
/// @brief Lock-free hand-over of values between one writer thread and one real-time reader thread.

#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace dio {

/// Copies a trivially copyable value into and out of an array of relaxed atomic words, so that a
/// reader racing with the writer is not a data race (the reader detects and discards the torn copy).
/// @tparam T the value type
template <typename T>
class AtomicWords {
  static_assert(std::is_trivially_copyable_v<T>, "AtomicWords carries plain data only");

 public:
  /// Writes `value` (with relaxed stores).
  void store(const T& value) {
    uint32_t raw[kWords] = {};
    std::memcpy(raw, &value, sizeof(T));
    for (size_t i = 0; i < kWords; ++i) words_[i].store(raw[i], std::memory_order_relaxed);
  }
  /// Reads the value (with relaxed loads); it is only meaningful if no store ran meanwhile.
  void load(T& value) const {
    uint32_t raw[kWords];
    for (size_t i = 0; i < kWords; ++i) raw[i] = words_[i].load(std::memory_order_relaxed);
    std::memcpy(&value, raw, sizeof(T));
  }

 private:
  /// Number of 32-bit words that hold a T.
  static constexpr size_t kWords = (sizeof(T) + 3) / 4;
  std::atomic<uint32_t> words_[kWords] = {};  ///< the value
};

/// The latest value of a plain-data type, written by one thread and read by one other without
/// locks, allocation or waiting (a "seqlock"). The writer never waits for the reader. The reader
/// gets a complete value, never a mix of two writes: if a write is in progress it retries a few times
/// and then reports failure, so that a real-time thread can carry on with the value it had.
///
/// Meant for the parameters the main loop hands to the audio thread: one copy of the whole struct,
/// so adding a field needs no change anywhere else.
/// @tparam T a trivially copyable type
template <typename T>
class Seqlock {
 public:
  /// Starts out holding a default-constructed value (a reader that comes first gets that, not zeros).
  Seqlock() { data_.store(T()); }

  /// Publishes `value`. Only one thread may call this.
  void store(const T& value) {
    const uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);  // odd: a write is in progress
    std::atomic_thread_fence(std::memory_order_release);
    data_.store(value);
    seq_.store(s + 2, std::memory_order_release);  // even: the value is complete
  }

  /// Reads the latest complete value into `value`.
  /// @param attempts how often to retry while a write is in progress
  /// @return false if no complete value could be read (`value` is then unchanged)
  bool load(T& value, int attempts = 4) const {
    for (int i = 0; i < attempts; ++i) {
      const uint32_t before = seq_.load(std::memory_order_acquire);
      if (before & 1u) continue;
      T copy;
      data_.load(copy);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seq_.load(std::memory_order_relaxed) == before) {
        value = copy;
        return true;
      }
    }
    return false;
  }

 private:
  std::atomic<uint32_t> seq_{0};  ///< even: stable; odd: being written; grows with every write
  AtomicWords<T> data_;           ///< the value
};

/// A queue of plain-data events from one writer thread to one reader thread, lock-free and
/// allocation-free. The writer never waits: when the queue is full the **oldest** events are dropped
/// (the reader skips ahead and counts them), so a slow reader hears the latest events, not stale ones.
/// @tparam T a trivially copyable event type
/// @tparam N capacity in events
template <typename T, size_t N>
class EventRing {
  static_assert(N >= 2, "a ring needs room for at least two events");

 public:
  /// Appends an event. Only one thread may call this.
  void push(const T& event) {
    const uint64_t n = written_.load(std::memory_order_relaxed);
    Slot& slot = slots_[n % N];
    slot.seq.store(2 * n + 1, std::memory_order_relaxed);  // odd: being written
    std::atomic_thread_fence(std::memory_order_release);
    slot.data.store(event);
    slot.seq.store(2 * n + 2, std::memory_order_release);  // even: complete, and says which event it is
    written_.store(n + 1, std::memory_order_release);
  }

  /// Takes the oldest event that is still there. Only one thread may call this.
  /// @return false if there is none
  bool pop(T& event) {
    for (;;) {
      const uint64_t w = written_.load(std::memory_order_acquire);
      if (read_ == w) return false;
      Slot& slot = slots_[read_ % N];
      const uint64_t before = slot.seq.load(std::memory_order_acquire);
      if (before != 2 * read_ + 2) {  // the writer lapped us and overwrote this event: it is lost (an older one cannot be here)
        ++dropped_;
        ++read_;
        continue;
      }
      T copy;
      slot.data.load(copy);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (slot.seq.load(std::memory_order_relaxed) != before) continue;  // overwritten while reading
      event = copy;
      ++read_;
      return true;
    }
  }

  /// How many events the reader has lost to overflow so far (read by the reader thread).
  uint64_t dropped() const { return dropped_; }

 private:
  /// One event and the sequence number that says which one it is and whether it is complete.
  struct Slot {
    std::atomic<uint64_t> seq{0};  ///< 2n+1 while event n is written, 2n+2 when it is complete
    AtomicWords<T> data;           ///< the event
  };
  Slot slots_[N];                       ///< the events, event n in slot n % N
  std::atomic<uint64_t> written_{0};    ///< how many events have been pushed
  uint64_t read_ = 0;                   ///< how many the reader has taken or dropped (reader thread only)
  uint64_t dropped_ = 0;                ///< how many the reader dropped (reader thread only)
};

}  // namespace dio
