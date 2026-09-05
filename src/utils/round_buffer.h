#pragma once

#include <cassert>
#include <cstdint>

template <typename T>
class RoundBuffer {
public:
    [[nodiscard]] RoundBuffer(const uint8_t size) : _size(size), _buffer(new T[size]), _head(0), _tail(0), _count(0) {}

    void add(const T& item) {
        _buffer[_head] = item;
        _head = (_head + 1) % _size;
        if (_count < _size) {
            _count++;
        } else {
            _tail = (_tail + 1) % _size;
        }
    }

    [[nodiscard]] T& get(const int index) const {
        if (index >= 0) {
            return getFromTail(index);
        }
        const int indexFromHead = -(index + 1);
        assert(indexFromHead < _count);
        return getFromHead(indexFromHead);
    }

    [[nodiscard]] T& getFromTail(const int index) const {
        assert(index >= 0 && index < _count);
        return _buffer[(_tail + index) % _size];
    }

    [[nodiscard]] T& getFromHead(const int index) const {
        assert(index >= 0 && index < _count);
        return _buffer[(_head + _size - 1 - index) % _size];
    }

    [[nodiscard]] T& peek() const {
        assert(_count > 0);
        return _buffer[_tail];
    }


    [[nodiscard]] uint8_t count() const {
        return _count;
    }

    ~RoundBuffer() {
        delete[] _buffer;
    }
private:
    uint8_t _size;
    T* _buffer;
    uint8_t _head;
    uint8_t _tail;
    uint8_t _count;
};
