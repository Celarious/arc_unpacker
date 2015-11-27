#pragma once

#include <string>
#include "fmt/image_decoder.h"

namespace au {
namespace fmt {
namespace twilight_frontier {

    class Pak2ImageDecoder final : public ImageDecoder
    {
    public:
        Pak2ImageDecoder();
        ~Pak2ImageDecoder();
        void clear_palettes();
        void add_palette(const std::string &name, const bstr &palette_data);

    protected:
        bool is_recognized_impl(io::File &input_file) const override;
        res::Image decode_impl(io::File &input_file) const override;

    private:
        struct Priv;
        std::unique_ptr<Priv> p;
    };

} } }
