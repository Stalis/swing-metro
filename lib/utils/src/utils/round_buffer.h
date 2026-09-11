#pragma once

#include <cassert>
#include <cstdint>

template <typename T>
class RoundBuffer {
  public:
    [[nodiscard]] RoundBuffer(uint8_t size);
    ~RoundBuffer();

    void add(const T& item);
    [[nodiscard]] T& get(int index) const;
    [[nodiscard]] T& getFromTail(int index) const;
    [[nodiscard]] T& getFromHead(int index) const;
    [[nodiscard]] T& peek() const;
    [[nodiscard]] uint8_t count() const;

  private:
    uint8_t _size;
    T* _buffer;
    uint8_t _head;
    uint8_t _tail;
    uint8_t _count;
};

#include "round_buffer.ipp"
