#include <iostream>
#include <vector>
#include <algorithm>
using namespace std;


class Graph {
private:
    int n;
    vector<vector<int>> adj;

    void DFS(int u, vector<bool>& vis) {
        vis[u] = true;
        for (int v : adj[u]) {
            if (!vis[v]) {
                DFS(v, vis);
            }
        }
    }

public:
    Graph(int n) : n(n) {
        adj.resize(n);
    }

    void addEdge(int u, int v) {
        adj[u].push_back(v);
        adj[v].push_back(u);
    }

    void connectEdge(int u, int v) {
        if (find(adj[u].begin(), adj[u].end(), v) == adj[u].end()) {
            adj[u].push_back(v);
        }
        if (find(adj[v].begin(), adj[v].end(), u) == adj[v].end()) {
            adj[v].push_back(u);
        }
    }

    void removeEdge(int u, int v) {
        adj[u].erase(remove(adj[u].begin(), adj[u].end(), v), adj[u].end());
        adj[v].erase(remove(adj[v].begin(), adj[v].end(), u), adj[v].end());
    }

    int addVertex() {
        adj.push_back({});
        return n++;
    }


    void removeVertex(int u) {
        if (u >= n) return;

        for (int i = 0; i < n; ++i) {
            if (i == u) continue;
            adj[i].erase(remove(adj[i].begin(), adj[i].end(), u), adj[i].end());
        }

        if (u != n - 1) {
            adj[u] = adj[n - 1];
            for (int i = 0; i < n - 1; ++i) {
                for (int& v: adj[i]) {
                    if (v== n - 1) v= u;
                    else if (v== u) v= u;
                }
            }
        }

        adj.pop_back();
        n--;
    }
    bool isConnected() {
        if(n == 1)return true;
        vector<bool> vis(n, false);

        DFS(0, vis);

        for (int i = 0; i < n; ++i) {
            if (!vis[i])
                return false;
        }
        return true;
    }

};
