#pragma once
#include "compat.h" 
#include "io.h"
#include "../mem.h"      

namespace mv {

template <typename T>
class Vector {
public:
    Vector() : data_(nullptr), size_(0), cap_(0) {}
    explicit Vector(unsigned long n) : data_(nullptr), size_(0), cap_(0) { resize(n); }
    Vector(unsigned long n, const T& val) : data_(nullptr), size_(0), cap_(0) { assign(n, val); }
    Vector(const Vector& other) : data_(nullptr), size_(0), cap_(0) {
        reserve(other.size_);
        for (unsigned long i = 0; i < other.size_; ++i)
            construct(data_ + i, other.data_[i]);
        size_ = other.size_;
    }
    Vector(Vector&& other) : data_(other.data_), size_(other.size_), cap_(other.cap_) {
        other.data_ = nullptr; other.size_ = 0; other.cap_ = 0;
    }
    Vector(const T* first, const T* last) : data_(nullptr), size_(0), cap_(0) {
        unsigned long n = (unsigned long)(last - first);
        reserve(n);
        for (unsigned long i = 0; i < n; ++i) construct(data_ + i, first[i]);
        size_ = n;
    }
    ~Vector() { clear(); if (data_) free(data_); }

    Vector& operator=(const Vector& other) {
        if (this == &other) return *this;
        clear(); reserve(other.size_);
        for (unsigned long i = 0; i < other.size_; ++i)
            construct(data_ + i, other.data_[i]);
        size_ = other.size_;
        return *this;
    }
    Vector& operator=(Vector&& other) {
        if (this == &other) return *this;
        clear(); if (data_) free(data_);
        data_ = other.data_; size_ = other.size_; cap_ = other.cap_;
        other.data_ = nullptr; other.size_ = 0; other.cap_ = 0;
        return *this;
    }

    unsigned long size() const { return size_; }
    bool empty() const { return size_ == 0; }
    unsigned long capacity() const { return cap_; }

    void reserve(unsigned long new_cap) {
        if (new_cap <= cap_) return;
        T* new_data = static_cast<T*>(malloc(new_cap * sizeof(T)));
        if (!new_data) { io::puts("Vector::reserve malloc failed\n"); io::exit(1); }
        for (unsigned long i = 0; i < size_; ++i) {
            construct(new_data + i, mv::move(data_[i]));
            destroy(data_ + i);
        }
        if (data_) free(data_);
        data_ = new_data; cap_ = new_cap;
    }
    void resize(unsigned long new_size) {
        if (new_size > cap_) reserve(new_size);
        for (unsigned long i = size_; i < new_size; ++i) construct(data_ + i);
        for (unsigned long i = new_size; i < size_; ++i) destroy(data_ + i);
        size_ = new_size;
    }
    void resize(unsigned long new_size, const T& val) {
        if (new_size > cap_) reserve(new_size);
        for (unsigned long i = size_; i < new_size; ++i) construct(data_ + i, val);
        for (unsigned long i = new_size; i < size_; ++i) destroy(data_ + i);
        size_ = new_size;
    }
    void assign(unsigned long n, const T& val) {
        clear(); reserve(n);
        for (unsigned long i = 0; i < n; ++i) construct(data_ + i, val);
        size_ = n;
    }
    void push_back(const T& val) {
        if (size_ >= cap_) grow();
        construct(data_ + size_, val); ++size_;
    }
    void push_back(T&& val) {
        if (size_ >= cap_) grow();
        construct(data_ + size_, mv::move(val)); ++size_;
    }
    template <typename... Args>
    void emplace_back(Args&&... args) {
        if (size_ >= cap_) grow();
        construct(data_ + size_, T(mv::forward<Args>(args)...));
        ++size_;
    }
    void pop_back() {
        if (size_ > 0) { --size_; destroy(data_ + size_); }
    }
    void erase(T* first, T* last) {
        for (T* p = first; p < last; ++p) destroy(p);
        T* dst = first;
        for (T* src = last; src < data_ + size_; ++src) {
            construct(dst, mv::move(*src));
            destroy(src);
            ++dst;
        }
        size_ = (unsigned long)(dst - data_);
    }

    void clear() {
        for (unsigned long i = 0; i < size_; ++i) destroy(data_ + i);
        size_ = 0;
    }
    T& operator[](unsigned long i) { return data_[i]; }
    const T& operator[](unsigned long i) const { return data_[i]; }
    T& front() { return data_[0]; }
    const T& front() const { return data_[0]; }
    T& back() { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }
    T* data() { return data_; }
    const T* data() const { return data_; }
    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

private:
    T* data_;
    unsigned long size_;
    unsigned long cap_;
    void grow() {
        unsigned long new_cap = (cap_ == 0) ? 4 : cap_ * 2;
        if (cap_ > (1ULL << 63)) { io::puts("Vector capacity overflow\n"); io::exit(1); }
        reserve(new_cap);
    }
    static void construct(T* p)               { new (static_cast<void*>(p)) T(); }
    static void construct(T* p, const T& val)  { new (static_cast<void*>(p)) T(val); }
    static void construct(T* p, T&& val)       { new (static_cast<void*>(p)) T(mv::move(val)); }
    static void destroy(T* p)                  { p->~T(); }
};

} // namespace mv