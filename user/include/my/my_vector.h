#pragma once
#include "../mem.h"  


namespace mv {

// 简单的 move:返回右值引用(裸机够用,不依赖 <utility>)
template <typename T>
struct remove_ref { using type = T; };
template <typename T>
struct remove_ref<T&> { using type = T; };
template <typename T>
struct remove_ref<T&&> { using type = T; };

template <typename T>
typename remove_ref<T>::type&& move(T&& x) {
    return static_cast<typename remove_ref<T>::type&&>(x);
}

template <typename T>
class Vector {
public:
    // ---- 构造 / 析构 ----
    Vector() : data_(nullptr), size_(0), cap_(0) {}

    explicit Vector(unsigned long n) : data_(nullptr), size_(0), cap_(0) {
        resize(n);
    }

    Vector(unsigned long n, const T& val) : data_(nullptr), size_(0), cap_(0) {
        assign(n, val);
    }

    // 拷贝构造
    Vector(const Vector& other) : data_(nullptr), size_(0), cap_(0) {
        reserve(other.size_);
        for (unsigned long i = 0; i < other.size_; ++i) {
            construct(data_ + i, other.data_[i]);
        }
        size_ = other.size_;
    }

    // 移动构造
    Vector(Vector&& other) : data_(other.data_), size_(other.size_), cap_(other.cap_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.cap_  = 0;
    }

    ~Vector() {
        clear();
        if (data_) free(data_);
    }

    // ---- 赋值 ----
    Vector& operator=(const Vector& other) {
        if (this == &other) return *this;
        clear();
        reserve(other.size_);
        for (unsigned long i = 0; i < other.size_; ++i) {
            construct(data_ + i, other.data_[i]);
        }
        size_ = other.size_;
        return *this;
    }

    Vector& operator=(Vector&& other) {
        if (this == &other) return *this;
        clear();
        if (data_) free(data_);
        data_ = other.data_;
        size_ = other.size_;
        cap_  = other.cap_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.cap_  = 0;
        return *this;
    }

    // ---- 容量 ----
    unsigned long size() const { return size_; }
    bool empty() const { return size_ == 0; }
    unsigned long capacity() const { return cap_; }

    void reserve(unsigned long new_cap) {
        if (new_cap <= cap_) return;
        T* new_data = static_cast<T*>(malloc(new_cap * sizeof(T)));
        // 把旧元素移动/拷贝到新内存
        for (unsigned long i = 0; i < size_; ++i) {
            construct(new_data + i, mv::move(data_[i]));
            destroy(data_ + i);
        }
        if (data_) free(data_);
        data_ = new_data;
        cap_ = new_cap;
    }

    void resize(unsigned long new_size) {
        if (new_size > cap_) {
            reserve(new_size);
        }
        // 增大:默认构造新元素
        for (unsigned long i = size_; i < new_size; ++i) {
            construct(data_ + i);
        }
        // 缩小:析构多余元素
        for (unsigned long i = new_size; i < size_; ++i) {
            destroy(data_ + i);
        }
        size_ = new_size;
    }

    void resize(unsigned long new_size, const T& val) {
        if (new_size > cap_) reserve(new_size);
        for (unsigned long i = size_; i < new_size; ++i) {
            construct(data_ + i, val);
        }
        for (unsigned long i = new_size; i < size_; ++i) {
            destroy(data_ + i);
        }
        size_ = new_size;
    }

    void assign(unsigned long n, const T& val) {
        clear();
        reserve(n);
        for (unsigned long i = 0; i < n; ++i) {
            construct(data_ + i, val);
        }
        size_ = n;
    }

    // ---- 增删 ----
    void push_back(const T& val) {
        if (size_ >= cap_) {
            grow();
        }
        construct(data_ + size_, val);
        ++size_;
    }

    void push_back(T&& val) {
        if (size_ >= cap_) {
            grow();
        }
        construct(data_ + size_, mv::move(val));
        ++size_;
    }

    void pop_back() {
        if (size_ > 0) {
            --size_;
            destroy(data_ + size_);
        }
    }

    void clear() {
        for (unsigned long i = 0; i < size_; ++i) {
            destroy(data_ + i);
        }
        size_ = 0;
    }

    // ---- 访问 ----
    T& operator[](unsigned long i) { return data_[i]; }
    const T& operator[](unsigned long i) const { return data_[i]; }

    T& front() { return data_[0]; }
    const T& front() const { return data_[0]; }

    T& back() { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }

    T* data() { return data_; }
    const T* data() const { return data_; }

    // ---- 迭代器(裸指针即可)----
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
        reserve(new_cap);
    }

    // placement new 构造(默认)
    static void construct(T* p) {
        new (static_cast<void*>(p)) T();
    }
    // placement new 构造(拷贝)
    static void construct(T* p, const T& val) {
        new (static_cast<void*>(p)) T(val);
    }
    // placement new 构造(移动)
    static void construct(T* p, T&& val) {
        new (static_cast<void*>(p)) T(mv::move(val));
    }
    // 析构
    static void destroy(T* p) {
        p->~T();
    }
};

} // namespace mv

// placement new 声明(裸机下要自己给;cpp_runtime.cpp 里有定义)
inline void* operator new(unsigned long, void* p) noexcept { return p; }