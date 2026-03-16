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
#include <filesystem>
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
    void save_grains_map(int min_grain_size, const std::filesystem::path&);
private:
    std::vector<std::vector<QuatF>> orientations;
    DJS2d djs; // grain disjoint set
};


#include <map>
#include <stb/stb_image_write.h>
inline void GrainSegmenter::save_grains_map(int min_grain_size, const std::filesystem::path &img_path) {
    struct RGB {
        int r, g, b;
    };
    int y = orientations.size();
    int x = orientations[0].size();
    std::vector<std::vector<RGB>> colors(y, std::vector<RGB>(x));
    std::map<std::pair<int, int>, RGB> colormp;
    for(int i = 0; i < y; i++ ) {
        for(int j = 0; j < x; j++ ) {
            if(!colormp.contains(djs.find({i, j}))) {
                colormp[djs.find({i, j})] = {
                    rand() % 255,
                    rand() % 255,
                    rand() % 255
                };
            }
            if(djs.size({i, j}) >= min_grain_size) {
                colors[i][j] = colormp[djs.find({i, j})];
            }
        }
    }
    
    std::vector<uint8_t> img;
    int w = x, h = y;
    img.reserve((size_t)w * h * 3);

    for (const auto& row : colors) {
        for (const auto& px : row) {
            auto [r, g, b] = px;

            img.push_back(static_cast<uint8_t>(r));
            img.push_back(static_cast<uint8_t>(g));
            img.push_back(static_cast<uint8_t>(b));
        }
    }
    int ok = stbi_write_png(img_path.string().c_str(), w, h, 3, img.data(), w * 3);
    if(!ok) std::cout << "failed" << std::endl;
};

