#pragma once
#include "random.h"

namespace my {
inline uint32_t treap_rand() {
    return (uint32_t)mymath::global_rng().next();
}

template<typename K, typename V>
class map {
    struct Node {
        K first;           // 标准命名：key
        V second;          // 标准命名：val
        uint32_t pri;
        Node *l = nullptr, *r = nullptr;
        Node(const K& k, const V& v) : first(k), second(v), pri(treap_rand()) {}
    };
    Node* root = nullptr;
    size_t n = 0;

    void rot_r(Node*& p) { Node* q = p->l; p->l = q->r; q->r = p; p = q; }
    void rot_l(Node*& p) { Node* q = p->r; p->r = q->l; q->l = p; p = q; }

    void insert(Node*& p, const K& k, const V& v) {
        if (!p) { p = new Node(k, v); ++n; return; }
        if (k < p->first) {
            insert(p->l, k, v);
            if (p->l->pri < p->pri) rot_r(p);
        } else if (k > p->first) {
            insert(p->r, k, v);
            if (p->r->pri < p->pri) rot_l(p);
        } else {
            p->second = v;
        }
    }
    Node* find_node(Node* p, const K& k) const {
        if (!p) return nullptr;
        if (k < p->first) return find_node(p->l, k);
        if (k > p->first) return find_node(p->r, k);
        return p;
    }
    void erase(Node*& p, const K& k) {
        if (!p) return;
        if (k < p->first) erase(p->l, k);
        else if (k > p->first) erase(p->r, k);
        else {
            if (!p->l && !p->r) { delete p; p = nullptr; --n; }
            else if (!p->r || (p->l && p->l->pri < p->r->pri)) {
                rot_r(p); erase(p->r, k);
            } else {
                rot_l(p); erase(p->l, k);
            }
        }
    }
    void clear(Node* p) {
        if (!p) return;
        clear(p->l); clear(p->r); delete p;
    }

public:
    ~map() { clear(root); }

    // 迭代器需要访问 root 做 ++
    struct iterator {
        Node* cur = nullptr;
        map* m = nullptr;

        iterator() = default;
        iterator(Node* c, map* mp) : cur(c), m(mp) {}

        iterator& operator++() {
            if (!cur || !m) return *this;
            if (cur->r) {
                cur = cur->r;
                while (cur->l) cur = cur->l;
            } else {
                Node* anc = nullptr;
                Node* n = m->root;
                while (n && n != cur) {
                    if (cur->first < n->first) {
                        anc = n;
                        n = n->l;
                    } else {
                        n = n->r;
                    }
                }
                cur = anc;
            }
            return *this;
        }
        bool operator!=(const iterator& o) const { return cur != o.cur; }
        bool operator==(const iterator& o) const { return cur == o.cur; }
        Node& operator*() const { return *cur; }
        Node* operator->() const { return cur; }
    };

    void insert(const K& k, const V& v) { insert(root, k, v); }

    iterator find(const K& k) {
        return iterator(find_node(root, k), this);
    }
    iterator end() { return iterator(); }

    bool count(const K& k) const { return find_node(root, k) != nullptr; }

    V& operator[](const K& k) {
        Node* p = find_node(root, k);
        if (p) return p->second;
        insert(root, k, V());
        return find_node(root, k)->second;
    }

    void erase(const K& k) { erase(root, k); }
    void clear() { clear(root); root = nullptr; n = 0; }
    bool empty() const { return n == 0; }
    size_t size() const { return n; }

    iterator begin() {
        Node* p = root;
        while (p && p->l) p = p->l;
        return iterator(p, this);
    }

    V* find_ptr(const K& k) {
        Node* p = find_node(root, k);
        return p ? &p->second : nullptr;
    }
    const V* find_ptr(const K& k) const {
        Node* p = find_node(root, k);
        return p ? &p->second : nullptr;
    }
};

}