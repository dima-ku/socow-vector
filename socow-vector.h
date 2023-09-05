#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

template <class T, size_t SMALL_SIZE>
class socow_vector {
public:
  using value_type = T;

  using reference = value_type&;
  using const_reference = const value_type&;

  using pointer = value_type*;
  using const_pointer = const value_type*;

  using iterator = pointer;
  using const_iterator = const_pointer;

public:
  socow_vector() : SZ(0), is_static(true) {}

  socow_vector(const socow_vector& other) : SZ(other.SZ), is_static(other.is_static) {
    if (other.is_static) {
      std::uninitialized_copy_n(other.staticBuffer, other.size(), staticBuffer);
    } else {
      dynamicBuffer = other.dynamicBuffer;
      dynamicBuffer->cntRef++;
    }
  }

  ~socow_vector() {
    if (is_static) {
      std::destroy_n(staticBuffer, SZ);
    } else {
      dropDynamicBuffer(dynamicBuffer, SZ);
    }
  }

  void swap(socow_vector& other) {
    if (this == &other) {
      return;
    } else if (is_static && other.is_static) {
      if (size() > other.size()) {
        other.swap(*this);
        return;
      }
      std::uninitialized_copy_n(other.staticBuffer + size(), other.size() - size(), staticBuffer + size());
      size_t backUpSize = size();
      SZ = other.size();
      for (size_t i = 0; i < backUpSize; i++) {
        std::swap(staticBuffer[i], other.staticBuffer[i]);
      }
      std::destroy_n(other.staticBuffer + backUpSize, other.size() - backUpSize);
      other.SZ = backUpSize;
      return;
    } else if (is_static ^ other.is_static) {
      if (!is_static) {
        other.swap(*this);
        return;
      }
      dynamic_buffer* backUpBuffer = other.dynamicBuffer;
      try {
        std::uninitialized_copy_n(staticBuffer, size(), other.staticBuffer);
      } catch (...) {
        other.dynamicBuffer = backUpBuffer;
        throw;
      }
      std::destroy_n(staticBuffer, size());
      dynamicBuffer = backUpBuffer;
      is_static = false;
      other.is_static = true;
    } else {
      std::swap(dynamicBuffer, other.dynamicBuffer);
    }
    std::swap(SZ, other.SZ);
  }

  socow_vector& operator=(const socow_vector& other) {
    if (this == &other) {
      return *this;
    } else if (is_static && !other.is_static) {
      std::destroy_n(staticBuffer, size());
      dynamicBuffer = other.dynamicBuffer;
      is_static = false;
      SZ = other.size();
      dynamicBuffer->cntRef++;
      return *this;
    } else if (is_static && other.is_static) {
      if (size() >= other.size()) {
        socow_vector tmp(other);
        swap(tmp);
        return *this;
      } else {
        std::uninitialized_copy_n(other.staticBuffer + size(), other.size() - size(), staticBuffer + size());
        try {
          socow_vector tmp;
          std::uninitialized_copy_n(other.staticBuffer, size(), tmp.staticBuffer);
          tmp.SZ = size();

          for (size_t i = 0; i < size(); i++) {
            std::swap(staticBuffer[i], tmp.staticBuffer[i]);
          }
        } catch (...) {
          std::destroy_n(staticBuffer + size(), other.size() - size());
          throw;
        }
        SZ = other.size();
        return *this;
      }
    } else if (!is_static && other.is_static) {
      dynamic_buffer* tmp = dynamicBuffer;
      try {
        std::uninitialized_copy_n(other.staticBuffer, other.size(), staticBuffer);
      } catch (...) {
        dynamicBuffer = tmp;
        throw;
      }
      dropDynamicBuffer(tmp, size());
      is_static = true;
      SZ = other.size();
      return *this;
    }
    socow_vector tmp(other);
    swap(tmp);
    return *this;
  }

  pointer data() {
    if (!is_static && dynamicBuffer->cntRef > 1) {
      unshare();
    }
    return is_static ? staticBuffer : dynamicBuffer->elements;
  }

  const_pointer data() const {
    return is_static ? staticBuffer : dynamicBuffer->elements;
  }

  reference operator[](size_t i) {
    return *(data() + i);
  }

  const_reference operator[](size_t i) const {
    return *(data() + i);
  }

  size_t size() const {
    return SZ;
  }

  size_t capacity() const {
    return is_static ? SMALL_SIZE : dynamicBuffer->capacity;
  }

  reference front() {
    return *data();
  }

  const_reference front() const {
    return *data();
  }

  reference back() {
    return *(data() + size() - 1);
  }

  const_reference back() const {
    return *(data() + size() - 1);
  }

  void push_back(const_reference value) {
    insert(std::as_const(*this).end(), value);
  }

  void pop_back() {
    if (is_static || dynamicBuffer->cntRef == 1) {
      SZ--;
      data()[SZ].~value_type();
      return;
    }
    dynamic_buffer* newBuffer = getBufferViaCopy(capacity(), dynamicBuffer->elements, size() - 1);
    SZ--;
    dropDynamicBuffer(dynamicBuffer, size());
    dynamicBuffer = newBuffer;
  }

  bool empty() const {
    return size() == 0;
  }

  void reserve(size_t newCapacity) {
    if (is_static) {
      if (newCapacity <= SMALL_SIZE) {
        return;
      }
      dynamic_buffer* newBuffer = getBufferViaCopy(newCapacity, staticBuffer, size());
      std::destroy_n(staticBuffer, size());
      dynamicBuffer = newBuffer;
      is_static = false;
    } else {
      if (newCapacity > SMALL_SIZE && ((dynamicBuffer->cntRef == 1 && capacity() < newCapacity) ||
                                       (dynamicBuffer->cntRef > 1 && size() < newCapacity))) {

        dynamic_buffer* newBuffer = getBufferViaCopy(newCapacity, dynamicBuffer->elements, size());
        dropDynamicBuffer(dynamicBuffer, size());
        dynamicBuffer = newBuffer;
      } else if (newCapacity <= SMALL_SIZE && size() <= SMALL_SIZE && dynamicBuffer->cntRef > 1) {
        dynamic_buffer* backUp = dynamicBuffer;
        try {
          std::uninitialized_copy_n(backUp->elements, size(), staticBuffer);
        } catch (...) {
          dynamicBuffer = backUp;
          throw;
        }
        dropDynamicBuffer(backUp, size());
        is_static = true;
      }
    }
  }

  void shrink_to_fit() {
    if (!is_static && size() != capacity() && size() > SMALL_SIZE) {
      dynamic_buffer* newBuffer = getBufferViaCopy(size(), dynamicBuffer->elements, size());
      dropDynamicBuffer(dynamicBuffer, size());
      dynamicBuffer = newBuffer;
    } else if (!is_static && size() != capacity() && size() <= SMALL_SIZE) {
      dynamic_buffer* backUpBuffer = dynamicBuffer;
      try {
        std::uninitialized_copy_n(backUpBuffer->elements, size(), staticBuffer);
      } catch (...) {
        dynamicBuffer = backUpBuffer;
        throw;
      }
      dropDynamicBuffer(backUpBuffer, size());
      is_static = true;
    }
  }

  void clear() {
    erase(std::as_const(*this).begin(), std::as_const(*this).end());
  }

  iterator begin() {
    return data();
  }

  iterator end() {
    return data() + size();
  }

  const_iterator begin() const {
    return data();
  }

  const_iterator end() const {
    return data() + size();
  }

  iterator insert(const_iterator pos, const_reference value) {
    if ((is_static || dynamicBuffer->cntRef == 1) && size() < capacity()) {
      size_t ind = pos - begin();
      new (data() + size()) value_type(value);
      SZ++;
      for (size_t i = size() - 1; i != ind; i--) {
        std::swap(data()[i], data()[i - 1]);
      }
      return begin() + ind;
    }
    size_t ind = pos - std::as_const(*this).begin();
    socow_vector tmp(size() == capacity() ? 2 * capacity() : capacity());
    std::uninitialized_copy_n(std::as_const(*this).data(), ind, tmp.data());
    tmp.SZ += ind;
    tmp.push_back(value);
    std::uninitialized_copy_n(std::as_const(*this).data() + ind, size() - ind, tmp.data() + ind + 1);
    tmp.SZ += size() - ind; // <=> tmp.SZ = size() + 1
    if (is_static) {
      clear();
    }
    swap(tmp);
    return begin() + ind;
  }

  iterator erase(const_iterator pos) {
    return erase(pos, pos + 1);
  }

  iterator erase(const_iterator first, const_iterator last) {
    size_t ind = first - std::as_const(*this).begin();
    size_t cnt = last - first;
    if (cnt == 0) {
      return is_static ? (staticBuffer + ind) : (dynamicBuffer->elements + ind);
    }
    if (is_static || dynamicBuffer->cntRef == 1) {
      for (size_t i = ind; i + cnt < size(); ++i) {
        std::swap(data()[i], data()[i + cnt]);
      }
      for (size_t i = 0; i < cnt; i++) {
        pop_back();
      }
      return begin() + ind;
    }
    assert(!is_static && dynamicBuffer->cntRef > 1);
    socow_vector tmp(capacity());
    std::uninitialized_copy_n(dynamicBuffer->elements, ind, tmp.data());
    tmp.SZ = ind;
    std::uninitialized_copy_n(dynamicBuffer->elements + ind + cnt, size() - cnt - ind, tmp.data() + ind);
    tmp.SZ = size() - cnt;
    swap(tmp);
    return begin() + ind;
  }

private:
  struct dynamic_buffer {
    size_t capacity;
    size_t cntRef;
    value_type elements[0];

    dynamic_buffer(size_t cap, size_t cnt) : capacity(cap), cntRef(cnt) {}

    dynamic_buffer(const dynamic_buffer& other) = delete;
  };

private:
  size_t SZ;
  bool is_static;

  union {
    dynamic_buffer* dynamicBuffer;
    value_type staticBuffer[SMALL_SIZE];
  };

private:
  explicit socow_vector(size_t neededCapacity) {
    SZ = 0;
    if (neededCapacity <= SMALL_SIZE) {
      is_static = true;
    } else {
      is_static = false;
      dynamicBuffer = allocate_buffer(neededCapacity);
    }
  }

  dynamic_buffer* allocate_buffer(size_t newCapacity) {
    size_t cntBytes = sizeof(dynamic_buffer) + sizeof(value_type) * newCapacity;
    std::byte* place = static_cast<std::byte*>(operator new(cntBytes));
    dynamic_buffer* result = new (place) dynamic_buffer{newCapacity, 1};
    return result;
  }

  void unshare() {
    assert(!is_static && dynamicBuffer->cntRef > 1);
    dynamic_buffer* newBuffer = getBufferViaCopy(capacity(), dynamicBuffer->elements, size());
    dynamicBuffer->cntRef--;
    dynamicBuffer = newBuffer;
  }

  dynamic_buffer* getBufferViaCopy(size_t capacity, const_pointer from, size_t cnt) {
    dynamic_buffer* newBuffer = allocate_buffer(capacity);
    try {
      std::uninitialized_copy_n(from, cnt, newBuffer->elements);
    } catch (...) {
      operator delete(newBuffer, sizeof(dynamic_buffer) + sizeof(value_type) * capacity);
      throw;
    }
    return newBuffer;
  }

  void dropDynamicBuffer(dynamic_buffer* buffer, size_t sz = 0) {
    buffer->cntRef--;
    if (buffer->cntRef == 0) {
      std::destroy_n(buffer->elements, sz);
      operator delete(buffer, sizeof(dynamic_buffer) + sizeof(value_type) * buffer->capacity);
    }
  }
};
