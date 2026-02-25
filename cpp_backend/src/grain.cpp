#include <EbsdLib/IO/EbsdImporter.h>
#include <EbsdLib/IO/HKL/CprReader.h>
#include <vector>
#include <memory>
#include <EbsdLib/Core/OrientationTransformation.hpp>
#include <EbsdLib/LaueOps/LaueOps.h>
#include <algorithm>
#include <ranges>
#include <map>
#include "ebsd.h"

using std::vector;
using std::pair;
using std::swap;
using std::cout;
using std::endl;

class DJS2d {
    int x, y;
    vector<pair<int, int>> parent;
    vector<int> sz;

    int to_ser(int i, int j) const {
        return i * y + j;
    }
    int to_ser(const pair<int, int>& pos) const {
        return to_ser(pos.first, pos.second);
    }
    
public:

    bool in_bound(int i, int j) const {
        return 0 <= i && i < x && 0 <= j && j < y;
    }

    DJS2d(int _x, int _y): x(_x), y(_y) {
        parent.resize(x * y);
        sz.assign(x * y, 1);

        for (int i = 0; i < x; i++) {
            for (int j = 0; j < y; j++) {
                parent[to_ser(i, j)] = {i, j};
            }
        }
    }

    pair<int, int> find(const pair<int, int>& u) {
        assert(in_bound(u.first, u.second));
        int id = to_ser(u);
        if (parent[id] == u) return u;
        return parent[id] = find(parent[id]);
    }

    bool unite(const pair<int, int>& u, const pair<int, int>& v) {
        assert(in_bound(u.first, u.second));
        assert(in_bound(v.first, v.second));

        auto ru = find(u);
        auto rv = find(v);
        if (ru == rv) return false;

        int iru = to_ser(ru), irv = to_ser(rv);

        // union by size
        if (sz[iru] < sz[irv]) {
            swap(ru, rv);
            swap(iru, irv);
        }

        parent[irv] = ru;
        sz[iru] += sz[irv];
        return true;
    }

    bool same(const pair<int,int>& u, const pair<int,int>& v) {
        return find(u) == find(v);
    }

    int size(const pair<int,int>& u) {
        auto ru = find(u);
        return sz[to_ser(ru)];
    }
};

std::vector<int> find_grains(const vector<vector<QuatF>> &orientations, int min_grain_size/*pixel*/) {
    int x = orientations.size();
    int y = orientations[0].size();
    DJS2d djs(x, y);
    auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
    const float toleranceRad = 10.0f * (M_PI / 180.0f);
    for(int i = 0; i < x; i++ ) {
        for(int j = 0; j < y; j++ ) {
            if(i >= 1) {
                OrientationF misor = ops->calculateMisorientation(orientations[i][j], orientations[i - 1][j]);
                float ax, ay, az, angle;
                misor.toAxisAngle(ax, ay, az, angle);
                if(angle <= toleranceRad) {
                    djs.unite({i, j}, {i - 1, j});
                }
            }
            if(j >= 1) {
                OrientationF misor = ops->calculateMisorientation(orientations[i][j], orientations[i][j - 1]);
                float ax, ay, az, angle;
                misor.toAxisAngle(ax, ay, az, angle);
                if(angle <= toleranceRad) {

                    djs.unite({i, j}, {i, j - 1});
                }
            }
        }
    }
    vector<int> grains;
    for(int i = 0; i < x; i++ ) {
        for(int j = 0; j < y; j++ ) {
            if(djs.find({i, j}) == pair<int, int>{i, j} && djs.size({i, j}) >= min_grain_size) {
                grains.push_back(djs.size({i, j}));
            }
        }
    }
    return grains;
}


// vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> grains_map(const vector<vector<QuatF>> &orientations, int min_grain_size = 10/*pixel*/) {
//     int x = orientations.size();
//     int y = orientations[0].size();
//     DJS2d djs(x, y);
//     auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
//     const float toleranceRad = 10.0f * (M_PI / 180.0f);
//     int a = 0, b = 0;
//     for(int i = 0; i < x; i++ ) {
//         for(int j = 0; j < y; j++ ) {
//             if(i >= 1) {
//                //  OrientationF misor = ops->calculateMisorientation(orientations[i][j], orientations[i - 1][j]);
//                 OrientationF misor = ops->calculateMisorientation(orientations[i - 1][j], orientations[i][j]);
//                 float ax, ay, az, angle;
//                 misor.toAxisAngle(ax, ay, az, angle);
//                 if(angle <= toleranceRad) {
//                     a++;
//                     djs.unite({i, j}, {i - 1, j});
//                 }
//                 else {
//                     //cout << angle <<endl;
//                 }
//             }
//             if(j >= 1) {
//                 OrientationF misor = ops->calculateMisorientation(orientations[i][j], orientations[i][j - 1]);
//                 float ax, ay, az, angle;
//                 misor.toAxisAngle(ax, ay, az, angle);
//                 if(angle <= toleranceRad) {
//                     b++;
//                     djs.unite({i, j}, {i, j - 1});
//                 }
//             }
//         }
//     }
//     int printed = 0;
//     for (int i = 1; i < x && printed < 20; ++i) {
//         for (int j = 1; j < y && printed < 20; ++j) {
//             float ax, ay, az, au, al;

//             auto mu = ops->calculateMisorientation(orientations[i][j], orientations[i-1][j]);
//             mu.toAxisAngle(ax, ay, az, au);

//             auto ml = ops->calculateMisorientation(orientations[i][j], orientations[i][j-1]);
//             ml.toAxisAngle(ax, ay, az, al);

//             bool up_ok = std::isfinite(au) && (au <= toleranceRad);
//             bool left_ok = std::isfinite(al) && (al <= toleranceRad);

//             if (!up_ok && left_ok) {
//                 std::cout << "(" << i << "," << j << ") "
//                         << "up_deg=" << au * 180.0 / M_PI
//                         << " left_deg=" << al * 180.0 / M_PI
//                         << " " << mu.size() << " " << ml.size()
//                         << "\n";
//                 printed++;
//             }
//         }
//     }
//     vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> img(x, vector<std::tuple<uint8_t, uint8_t, uint8_t>>(y));
//     std::map<pair<int, int>, std::tuple<uint8_t, uint8_t, uint8_t>> colors;
//     for(int i = 0; i < x; i++ ) {
//         for(int j = 0; j < y; j++ ) {
//             auto [u, v] = djs.find({i, j});
//             if(!colors.count({u, v})) {
//                 colors[{u, v}] = {
//                     static_cast<uint8_t>(rand() % 256), 
//                     static_cast<uint8_t>(rand() % 256), 
//                     static_cast<uint8_t>(rand() % 256)
//                 };
//             }
//             img[i][j] = colors[{u, v}];
//         }
//     }
//     cout << a << " " << b << endl;
//     return img;
// }