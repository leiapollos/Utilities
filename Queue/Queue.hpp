#pragma once

#include <stdexcept>

template <typename T>
class Queue {
private:
    T* _buffer;
    int _head;
    int _tail;
    int _capacity;
public:
    Queue(int capacity);
    ~Queue();

    void enqueue(T value);
    template <typename... Args>
    void emplaceEnqueue(Args&&... args);
    void dequeue();
    T& front();
    const T& front() const;
    bool empty() const;
};


#pragma region Implementation

template <typename T>
Queue<T>::Queue(int capacity) :
    _buffer(new T[capacity + 1]),
    _head(0), 
    _tail(0), 
    _capacity(capacity + 1) {}

template <typename T>
Queue<T>::~Queue() {
    delete[] _buffer;
}

template <typename T>
void Queue<T>::enqueue(T value) {
    _buffer[_tail] = value;
    _tail = (_tail + 1) % _capacity;
    if (_tail == _head) {
        _head = (_head + 1) % _capacity;
    }
}

template <typename T>
template <typename... Args>
void Queue<T>::emplaceEnqueue(Args&&... args) {
    new (&_buffer[_tail]) T(std::forward<Args>(args)...);
    _tail = (_tail + 1) % _capacity;
    if (_tail == _head) {
        _head = (_head + 1) % _capacity;
    }
}

template <typename T>
void Queue<T>::dequeue() {
    if (_head != _tail) {
        _head = (_head + 1) % _capacity;
    }
}

template <typename T>
T& Queue<T>::front() {
    if (_head != _tail) {
        return _buffer[_head];
    }
    else {
        throw std::out_of_range("Queue is empty");
    }
}

template <typename T>
const T& Queue<T>::front() const {
    if (_head != _tail) {
        return _buffer[_head];
    }
    else {
        throw std::out_of_range("Queue is empty");
    }
}

template <typename T>
bool Queue<T>::empty() const {
    return _head == _tail;
}

#pragma endregion