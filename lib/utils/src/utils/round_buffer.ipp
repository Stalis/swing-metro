#pragma once
#include "round_buffer.h"

template <typename T>
RoundBuffer<T>::RoundBuffer(uint8_t size)
    : _size(size), _buffer(new T[size]), _head(0), _tail(0), _count(0) {}

template <typename T>
RoundBuffer<T>::~RoundBuffer() {
    delete[] _buffer;
}

template <typename T>
void RoundBuffer<T>::add(const T& item) {
    _buffer[_head] = item;
    _head = (_head + 1) % _size;
    if (_count < _size) {
        _count++;
    } else {
        _tail = (_tail + 1) % _size;
    }
}

template <typename T>
T& RoundBuffer<T>::get(int index) const {
    if (index >= 0) {
        return getFromTail(index);
    }
    const int indexFromHead = -(index + 1);
    assert(indexFromHead < _count);
    return getFromHead(indexFromHead);
}

template <typename T>
T& RoundBuffer<T>::getFromTail(int index) const {
    assert(index >= 0 && index < _count);
    return _buffer[(_tail + index) % _size];
}

template <typename T>
T& RoundBuffer<T>::getFromHead(int index) const {
    assert(index >= 0 && index < _count);
    return _buffer[(_head + _size - 1 - index) % _size];
}

template <typename T>
T& RoundBuffer<T>::peek() const {
    assert(_count > 0);
    return _buffer[_tail];
}

template <typename T>
uint8_t RoundBuffer<T>::count() const {
    return _count;
}
