#pragma once

#include <vector>
#include <utility>
#include <EbsdLib/Core/Quaternion.hpp>
#include <EbsdLib/Core/OrientationTransformation.hpp>


class DJS2d {
    int x, y;
    std::vector<std::pair<int, int>> parent;
    std::vector<int> sz;

    int to_ser(int i, int j) const {
        return i * y + j;
    }
    int to_ser(const std::pair<int, int>& pos) const {
        return to_ser(pos.first, pos.second);
    }
    
public:

    bool in_bound(int i, int j) const {
        return 0 <= i && i < x && 0 <= j && j < y;
    }

    void init(int _x, int _y) {
        x = _x;
        y = _y;
        parent.resize(x * y);
        sz.assign(x * y, 1);

        for (int i = 0; i < x; i++) {
            for (int j = 0; j < y; j++) {
                parent[to_ser(i, j)] = {i, j};
            }
        }
    }

    std::pair<int, int> find(const std::pair<int, int>& u) {
        assert(in_bound(u.first, u.second));
        int id = to_ser(u);
        if (parent[id] == u) return u;
        return parent[id] = find(parent[id]);
    }

    bool unite(const std::pair<int, int>& u, const std::pair<int, int>& v) {
        assert(in_bound(u.first, u.second));
        assert(in_bound(v.first, v.second));

        auto ru = find(u);
        auto rv = find(v);
        if (ru == rv) return false;

        int iru = to_ser(ru), irv = to_ser(rv);

        // union by size
        if (sz[iru] < sz[irv]) {
            std::swap(ru, rv);
            std::swap(iru, irv);
        }

        parent[irv] = ru;
        sz[iru] += sz[irv];
        return true;
    }

    bool same(const std::pair<int,int>& u, const std::pair<int,int>& v) {
        return find(u) == find(v);
    }

    int size(const std::pair<int,int>& u) {
        auto ru = find(u);
        return sz[to_ser(ru)];
    }
};

struct Grain {
    int size;
    QuatF orientation;
};

class GrainSegmenter {
public:
    GrainSegmenter(float* euler1, float* euler2, float* euler3, int Xdim, int Ydim) {
        orientations.resize(Ydim, std::vector<QuatF>(Xdim));
        for (int y = 0; y < Ydim; ++y) {
            for (int x = 0; x < Xdim; ++x) {
                int p = y * Xdim + x;
                std::vector<float> euler = {euler1[p], euler2[p], euler3[p]};
                auto qu = OrientationTransformation::eu2qu<std::vector<float>, QuatF>(euler);
                orientations[y][x] = qu;
            }
        }
    }
    GrainSegmenter(const std::vector<std::vector<QuatF>> &_orientations): orientations(_orientations) {}
    std::vector<Grain> find_grains(int min_grain_size = 10 /* pixels */);
private:
    std::vector<std::vector<QuatF>> orientations;
    DJS2d djs; // grain disjoint set
};