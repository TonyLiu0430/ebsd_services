#include "band_contrast_map.h"
#include "ebsd_data.h"
#include "scale_bar.h"

#include <stb/stb_image_write.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>

void save_band_contrast_map(const std::filesystem::path& ebsd_path, const std::filesystem::path& img_path, bool include_scale_bar)
{
    const EbsdData data = load_ebsd_data(ebsd_path);
    const int x_dim = data.x_dim;
    const int y_dim = data.y_dim;
    const size_t point_count = static_cast<size_t>(x_dim) * y_dim;
    if(data.band_contrast.size() != point_count) {
        throw std::invalid_argument("Band contrast data is not available for " + ebsd_path.extension().string() + " files");
    }

    const int upscale = map_upscale(x_dim, y_dim);
    RasterImage image = upscale_nearest(data.band_contrast, x_dim, y_dim, 1, upscale);
    if(include_scale_bar) {
        image = add_scale_bar_footer(
            image.pixels,
            image.width,
            image.height,
            image.channels,
            data.x_step,
            x_dim,
            y_dim,
            "BC",
            upscale
        );
    }

    if(!img_path.parent_path().empty()) std::filesystem::create_directories(img_path.parent_path());
    if(stbi_write_png(
        img_path.string().c_str(),
        image.width,
        image.height,
        image.channels,
        image.pixels.data(),
        image.width * image.channels
    ) == 0) {
        throw std::runtime_error("Failed to write band contrast PNG");
    }
}
