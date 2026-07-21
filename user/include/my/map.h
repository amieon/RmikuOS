#pragma once
#include "random.h"

namespace my {
inline uint32_t treap_rand() {
    return (uint32_t)mymath::global_rng().next();
}
template<typename K, typename V>
class map {
    struct Node {
        K key;
        V val;
        uint32_t pri;
        Node *l = nullptr, *r = nullptr;
        Node(const K& k, const V& v) : key(k), val(v), pri(treap_rand()) {}
    };
    Node* root = nullptr;
    size_t n = 0;

    void rot_r(Node*& p) { Node* q = p->l; p->l = q->r; q->r = p; p = q; }
    void rot_l(Node*& p) { Node* q = p->r; p->r = q->l; q->l = p; p = q; }

    void insert(Node*& p, const K& k, const V& v) {
        if (!p) { p = new Node(k, v); ++n; return; }
        if (k < p->key) {
            insert(p->l, k, v);
            if (p->l->pri < p->pri) rot_r(p);
        } else if (k > p->key) {
            insert(p->r, k, v);
            if (p->r->pri < p->pri) rot_l(p);
        } else {
            p->val = v;
        }
    }
    Node* find(Node* p, const K& k) const {
        if (!p) return nullptr;
        if (k < p->key) return find(p->l, k);
        if (k > p->key) return find(p->r, k);
        return p;
    }
    void erase(Node*& p, const K& k) {
        if (!p) return;
        if (k < p->key) erase(p->l, k);
        else if (k > p->key) erase(p->r, k);
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
    void insert(const K& k, const V& v) { insert(root, k, v); }
    V* find(const K& k) { return find(root, k) ? &find(root, k)->val : nullptr; }
    const V* find(const K& k) const { return find(root, k) ? &find(root, k)->val : nullptr; }
    bool count(const K& k) const { return find(root, k) != nullptr; }
    V& operator[](const K& k) {
        Node* p = find(root, k);
        if (p) return p->val;
        insert(root, k, V());
        return *find(root, k);
    }
    void erase(const K& k) { erase(root, k); }
    void clear() { clear(root); root = nullptr; n = 0; }
    bool empty() const { return n == 0; }
    size_t size() const { return n; }
};

}