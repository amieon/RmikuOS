#pragma once
#include "my/map.h"

namespace my {

template<typename K>
class set {
    map<K, char> m;

public:
    void insert(const K& k) { m[k] = 1; }
    bool count(const K& k) const { return m.count(k); }
    void erase(const K& k) { m.erase(k); }
    bool empty() const { return m.empty(); }
    size_t size() const { return m.size(); }
    void clear() { m.clear(); }

    // 包装迭代器：解引用只返回 key
    struct iterator {
        typename map<K, char>::iterator it;
        iterator() = default;
        iterator(typename map<K, char>::iterator i) : it(i) {}

        iterator& operator++() { ++it; return *this; }
        bool operator!=(const iterator& o) const { return it != o.it; }
        bool operator==(const iterator& o) const { return it == o.it; }
        K& operator*() const { return it->first; }
        K* operator->() const { return &it->first; }
    };

    iterator begin() { return iterator(m.begin()); }
    iterator end() { return iterator(m.end()); }

    // const 迭代器（如果需要）
    struct const_iterator {
        typename map<K, char>::iterator it;  // map 没有 const_iterator，先用普通
        const_iterator() = default;
        const_iterator(typename map<K, char>::iterator i) : it(i) {}

        const_iterator& operator++() { ++it; return *this; }
        bool operator!=(const const_iterator& o) const { return it != o.it; }
        bool operator==(const const_iterator& o) const { return it == o.it; }
        const K& operator*() const { return it->first; }
        const K* operator->() const { return &it->first; }
    };

    const_iterator begin() const { return const_iterator(m.begin()); }
    const_iterator end() const { return const_iterator(m.end()); }
};

} // namespace my