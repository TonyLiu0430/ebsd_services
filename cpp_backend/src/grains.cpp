#include "grains.h"
#include <EbsdLib/LaueOps/LaueOps.h>

std::vector<Grain> GrainSegmenter::find_grains(int min_grain_size) {
    int y = orientations.size();
    int x = orientations[0].size();
    djs.init(y, x);
    auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
    const float toleranceRad = 10.0f * (M_PI / 180.0f);
    for(int i = 0; i < y; i++ ) {
        for(int j = 0; j < x; j++ ) {
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
            if(i + 1 < y) {
                OrientationF misor = ops->calculateMisorientation(orientations[i][j], orientations[i + 1][j]);
                float ax, ay, az, angle;
                misor.toAxisAngle(ax, ay, az, angle);
                if(angle <= toleranceRad) {
                    djs.unite({i, j}, {i + 1, j});
                }
            }
            if(j + 1 < x) {
                OrientationF misor = ops->calculateMisorientation(orientations[i][j], orientations[i][j + 1]);
                float ax, ay, az, angle;
                misor.toAxisAngle(ax, ay, az, angle);
                if(angle <= toleranceRad) {
                    djs.unite({i, j}, {i, j + 1});
                }
            }
        }
    }
    std::vector<Grain> grains;
    for(int i = 0; i < y; i++ ) {
        for(int j = 0; j < x; j++ ) {
            if(djs.find({i, j}) == std::pair<int, int>{i, j} && djs.size({i, j}) >= min_grain_size) {
                Grain g;
                g.size = djs.size({i, j});
                g.orientation = orientations[i][j];
                grains.push_back(g);
            }
        }
    }
    std::sort(grains.begin(), grains.end(), [](const Grain& a, const Grain& b) {
        return a.size > b.size;
    });
    return grains;
}

