#pragma once

#include <initializer_list>
#include <stdexcept>

template <typename T>
class Vector {
public:
    Vector();
    Vector(std::initializer_list<T> init);
    Vector(const Vector& other);
    Vector(Vector&& other) noexcept;
    ~Vector();

    Vector& operator=(const Vector& other);
    Vector& operator=(Vector&& other) noexcept;

    void pushBack(const T& value);
    void pushBack(T&& value);
    template <typename... Args>
    void emplaceBack(Args&&... args);
    void popBack();
    void insert(size_t index, const T& value);
    void insert(size_t index, T&& value);
    void erase(size_t index);
    void fastErase(size_t index); // Does not keep element order
    T& operator[](size_t index);
    const T& operator[](size_t index) const;
    size_t getSize() const;
    size_t getCapacity() const;
    void reserve(size_t newCapacity);
    void resize(size_t newSize, const T& value = T()); 
    bool empty() const;
    void clear();
    T& front();
    const T& front() const;
    T& back();
    const T& back() const;
    void shrinkToFit();
    void swap(Vector& other);


    class iterator;
    class constIterator;

    iterator begin() const;
    iterator end() const;
    constIterator cbegin() const;
    constIterator cend() const;

private:
    T* _data;
    size_t _size;
    size_t _capacity;

    void reallocate(size_t newCapacity);
};

#pragma region Implementation

template <typename T>
Vector<T>::Vector() {
    _data = new T[1];
    _size = 0;
    _capacity = 1;
}

template <typename T>
Vector<T>::~Vector() {
    delete[] _data;
}

template <typename T>
Vector<T>::Vector(std::initializer_list<T> init) {
    _size = init.size();
    _capacity = _size ? 2 * _size : 1;
    _data = new T[_capacity];

    size_t index = 0;
    for (const auto& element : init) {
        _data[index++] = element;
    }
}

template <typename T>
Vector<T>::Vector(const Vector& other) {
    _size = other._size;
    _capacity = other._capacity;
    _data = new T[_capacity];

    for (size_t i = 0; i < _size; ++i) {
        _data[i] = other._data[i];
    }
}

template <typename T>
Vector<T>::Vector(Vector&& other) noexcept {
    _size = other._size;
    _capacity = other._capacity;
    _data = other._data;

    other._size = 0;
    other._capacity = 0;
    other._data = nullptr;
}

template <typename T>
Vector<T>& Vector<T>::operator=(const Vector& other) {
    if (this == &other) {
        return *this;
    }

    T* tempData = new T[other._capacity];

    for (size_t i = 0; i < other._size; ++i) {
        tempData[i] = other._data[i];
    }

    delete[] _data;
    _data = tempData;
    _size = other._size;
    _capacity = other._capacity;

    return *this;
}

template <typename T>
Vector<T>& Vector<T>::operator=(Vector&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    delete[] _data;

    _size = other._size;
    _capacity = other._capacity;
    _data = other._data;

    other._size = 0;
    other._capacity = 0;
    other._data = nullptr;

    return *this;
}

template <typename T>
void Vector<T>::pushBack(const T& value) {
    if (_size == _capacity) {
        T* temp = new T[_capacity + _capacity / 2];
        for (int i = 0; i < _capacity; i++) {
            temp[i] = _data[i];
        }
        delete[] _data;
        _capacity += _capacity / 2;
        _data = temp;
    }
    _data[_size] = value;
    _size++;
}

template <typename T>
void Vector<T>::pushBack(T&& value) {
    if (_size == _capacity) {
        T* temp = new T[_capacity + _capacity / 2];
        for (int i = 0; i < _capacity; i++) {
            temp[i] = _data[i];
        }
        delete[] _data;
        _capacity += _capacity / 2;
        _data = temp;
    }
    _data[_size] = value;
    _size++;
}

template <typename T>
template <typename... Args>
void Vector<T>::emplaceBack(Args&&... args) {
    if (_size == _capacity) {
        T* temp = new T[_capacity + _capacity / 2];
        for (int i = 0; i < _size; i++) {
            temp[i] = std::move(_data[i]);
        }
        delete[] _data;
        _capacity += _capacity / 2;
        _data = temp;
    }
    new (&_data[_size]) T(std::forward<Args>(args)...);
    _size++;
}

template <typename T>
void Vector<T>::popBack() {
    _size--;
}

template <typename T>
void Vector<T>::insert(size_t index, const T& value) {
    if (_size == _capacity) {
        T* temp = new T[_capacity + _capacity / 2];
        for (int i = 0; i < _capacity; i++) {
            temp[i] = _data[i];
        }
        delete[] _data;
        _capacity += _capacity / 2;
        _data = temp;
    }
    for (int i = _size; i > index; i--) {
        _data[i] = _data[i - 1];
    }
    _data[index] = value;
    _size++;
}

template <typename T>
void Vector<T>::insert(size_t index, T&& value) {
    if (_size == _capacity) {
        T* temp = new T[_capacity + _capacity / 2];
        for (int i = 0; i < _capacity; i++) {
            temp[i] = _data[i];
        }
        delete[] _data;
        _capacity += _capacity / 2;
        _data = temp;
    }
    for (int i = _size; i > index; i--) {
        _data[i] = _data[i - 1];
    }
    _data[index] = value;
    _size++;
}

template <typename T>
void Vector<T>::erase(size_t index) {
    for (int i = index; i < _size - 1; i++) {
        _data[i] = _data[i + 1];
    }
    _size--;
}

template <typename T>
void Vector<T>::fastErase(size_t index) {
    _data[index] = _data[_size - 1];
    _size--;
}

template <typename T>
T& Vector<T>::operator[](size_t index) {
    if (index >= _size) {
        throw std::out_of_range("Index out of range");
    }
    return _data[index];
}

template <typename T>
const T& Vector<T>::operator[](size_t index) const {
    if (index >= _size) {
        throw std::out_of_range("Index out of range");
    }
    return _data[index];
}

template <typename T>
size_t Vector<T>::getSize() const {
    return _size;
}

template <typename T>
size_t Vector<T>::getCapacity() const {
    return _capacity;
}

template <typename T>
void Vector<T>::reserve(size_t newCapacity) {
    if (newCapacity > _capacity) {
        reallocate(newCapacity);
    }
}

template <typename T>
void Vector<T>::resize(size_t newSize, const T& value) {
    if (newSize > _size) {
        if (newSize > _capacity) {
            reallocate(newSize * 2);
        }
        for (size_t i = _size; i < newSize; ++i) {
            _data[i] = value;
        }
    }
    _size = newSize;
}

template <typename T>
bool Vector<T>::empty() const {
    return _size == 0;
}

template <typename T>
void Vector<T>::clear() {
    _size = 0;
}

template <typename T>
T& Vector<T>::front() {
    if (empty()) {
        throw std::out_of_range("Vector is empty");
    }
    return _data[0];
}

template <typename T>
const T& Vector<T>::front() const {
    if (empty()) {
        throw std::out_of_range("Vector is empty");
    }
    return _data[0];
}

template <typename T>
T& Vector<T>::back() {
    if (empty()) {
        throw std::out_of_range("Vector is empty");
    }
    return _data[_size - 1];
}

template <typename T>
const T& Vector<T>::back() const {
    if (empty()) {
        throw std::out_of_range("Vector is empty");
    }
    return _data[_size - 1];
}

template <typename T>
void Vector<T>::shrinkToFit() {
    if (_capacity > _size) {
        T* temp = new T[_size];
        for (size_t i = 0; i < _size; ++i) {
            temp[i] = std::move(_data[i]);
        }
        delete[] _data;
        _data = temp;
        _capacity = _size;
    }
}

template <typename T>
void Vector<T>::reallocate(size_t newCapacity) {
    if (newCapacity <= _capacity) return;
    T* temp = new T[newCapacity];
    for (size_t i = 0; i < _size; i++) {
        temp[i] = std::move(_data[i]);
    }
    delete[] _data;
    _data = temp;
    _capacity = newCapacity;
}

template <typename T>
void Vector<T>::swap(Vector& other) {
    std::swap(_size, other._size);
    std::swap(_capacity, other._capacity);
    std::swap(_data, other._data);
}

#pragma endregion


#pragma region Iterators

template <typename T>
class Vector<T>::iterator {
private:
    T* ptr;

public:
    iterator(T* ptr) : ptr(ptr) {}

    iterator& operator++() {
        ptr++;
        return *this;
    }

    iterator operator++(int) {
        iterator temp(*this);
        ptr++;
        return temp;
    }

    iterator& operator--() {
        ptr--;
        return *this;
    }

    iterator operator--(int) {
        iterator temp(*this);
        ptr--;
        return temp;
    }

    bool operator==(const iterator& other) const {
        return ptr == other.ptr;
    }

    bool operator!=(const iterator& other) const {
        return ptr != other.ptr;
    }

    T& operator*() const {
        return *ptr;
    }
};

template <typename T>
class Vector<T>::constIterator {
private:
    const T* ptr;

public:
    constIterator(const T* ptr) : ptr(ptr) {}

    constIterator& operator++() {
        ptr++;
        return *this;
    }

    constIterator operator++(int) {
        constIterator temp(*this);
        ptr++;
        return temp;
    }

    constIterator& operator--() {
        ptr--;
        return *this;
    }

    constIterator operator--(int) {
        constIterator temp(*this);
        ptr--;
        return temp;
    }

    bool operator==(const constIterator& other) const {
        return ptr == other.ptr;
    }

    bool operator!=(const constIterator& other) const {
        return ptr != other.ptr;
    }

    const T& operator*() const {
        return *ptr;
    }
};

template <typename T>
typename Vector<T>::iterator Vector<T>::begin() const {
    return iterator(_data);
}

template <typename T>
typename Vector<T>::iterator Vector<T>::end() const {
    return iterator(_data + _size);
}

template <typename T>
typename Vector<T>::constIterator Vector<T>::cbegin() const {
    return constIterator(_data);
}

template <typename T>
typename Vector<T>::constIterator Vector<T>::cend() const {
    return constIterator(_data + _size);
}

#pragma endregion
