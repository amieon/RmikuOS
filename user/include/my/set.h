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

    typename map<K, char>::iterator begin() { return m.begin(); }
    typename map<K, char>::iterator end() { return m.end(); }
};

} // namespace my