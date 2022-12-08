// Eng3D - General purpouse game engine
// Copyright (C) 2021, Eng3D contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------
// Name:
//      texture.hpp
//
// Abstract:
//      This file implements a texture which is based from the binary image
//      class to avoid code repetition. The difference of the texture from the
//      binary image is that the texture is oriented towards OpenGL rendering
//      more than the aforementioned binary image. A binary image is usable on
//      any context but rendering, while the texture is intended to be used in
//      rendering cases only.
// ----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>

#include "eng3d/ttf.hpp"
#include "eng3d/binary_image.hpp"
#include "eng3d/color.hpp"

struct SDL_Surface;
namespace Eng3D::IO {
    namespace Asset {
        class Base;
    };
};

namespace Eng3D {
    class State;
    class TextureManager;

    class TextureException: public BinaryImageException {
    public:
        TextureException(const std::string& filename, const std::string& message)
            : BinaryImageException(filename, message)
        {

        }
    };

    class TextureOptions {
    public:
        TextureOptions() = default;
        enum Target {
            TEXTURE_2D,
        } target = Eng3D::TextureOptions::Target::TEXTURE_2D;
        enum Wrap {
            REPEAT,
            CLAMP_TO_EDGE,
        } wrap_s = Eng3D::TextureOptions::Wrap::REPEAT;
        Wrap wrap_t = Eng3D::TextureOptions::Wrap::REPEAT;
        enum Filter {
            NEAREST,
            LINEAR,
            LINEAR_MIPMAP,
            NEAREST_MIPMAP,
            NEAREST_LINEAR_MIPMAP,
            LINEAR_NEAREST_MIPMAP,
        } min_filter = Eng3D::TextureOptions::Filter::NEAREST;
        Filter mag_filter = Eng3D::TextureOptions::Filter::NEAREST;
        enum Format {
            RGBA,
            RED,
            RGB32F,
            SRGB,
            SRGB_ALPHA,
        } format = Eng3D::TextureOptions::Format::RGBA;
        Format internal_format = Eng3D::TextureOptions::Format::RGBA;
        enum Type {
            UNSIGNED_BYTE,
        } type = Eng3D::TextureOptions::Type::UNSIGNED_BYTE;
        bool editable = false;
        bool compressed = true;
        bool instant_upload = false; // Has to be uploaded immediately and not async
        
        constexpr bool operator==(const TextureOptions& o) const {
            return target == o.target && wrap_s == o.wrap_s && wrap_t == o.wrap_t && min_filter == o.min_filter && mag_filter == o.mag_filter && internal_format == o.internal_format && format == o.format && type == o.type && editable == o.editable;
        }
    };
    const TextureOptions default_options;

    class Texture: public Eng3D::BinaryImage {
        void _upload(TextureOptions options = default_options);
        void _upload(SDL_Surface* surface);
    public:
        Texture() = default;
        Texture(const std::string& path);
        Texture(const Eng3D::IO::Asset::Base* asset);
        Texture(size_t width, size_t height, size_t bpp = 32);
        ~Texture() override;
        void create_dummy();
        void upload(TextureOptions options = default_options);
        void upload(SDL_Surface* surface);
        void gen_mipmaps() const;
        void bind() const;
        void delete_gputex();
        void guillotine(const Eng3D::Texture& map, int x, int y, int w, int h);
        void to_file(const std::string& filename);
        
        unsigned int id = 0;
        bool managed = false;
        friend class Eng3D::TextureManager;
    };

    // Array of textures
    class TextureArray: public Eng3D::BinaryImage {
    public:
        TextureArray(const std::string& path, size_t _tiles_x, size_t _tiles_y);
        void upload();
        size_t layers;
        size_t tiles_x, tiles_y;
        unsigned int id = 0;
    };

    template <class T>
    inline void hash_combine(std::size_t& s, const T& v) {
        std::hash<T> h;
        s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
    }

    /// @brief Texture map has implementation
    struct TextureMapHash {
        inline std::size_t operator()(const std::pair<std::string, TextureOptions>& key) const {
            std::size_t res = 0;
            hash_combine(res, key.first);
            TextureOptions s = key.second;
            hash_combine(res, s.target);
            hash_combine(res, s.wrap_s);
            hash_combine(res, s.wrap_t);
            hash_combine(res, s.min_filter);
            hash_combine(res, s.mag_filter);
            hash_combine(res, s.internal_format);
            hash_combine(res, s.format);
            hash_combine(res, s.type);
            hash_combine(res, s.editable);
            return res;
        }
    };

    class TextureUploadRequest {
    public:
        Texture* texture;
        TextureOptions options;
        SDL_Surface* surface = nullptr;
    };

    /// @brief General manager for textures, caches textures into the memory instead of reading them off the disk
    /// every time they need to be accessed.
    class TextureManager {
    private:
        std::unordered_map<std::pair<std::string, TextureOptions>, std::shared_ptr<Eng3D::Texture>, TextureMapHash> textures;
        std::vector<TextureUploadRequest> unuploaded_textures; // Textures that needs to be uploaded
        std::mutex unuploaded_lock;
        std::shared_ptr<Eng3D::Texture> white;
        /// @brief Stores the text textures
        /// @todo Take in account colour and font for creating the key, since repeated text will be displayed incorrectly
        std::unordered_map<std::string, std::shared_ptr<Eng3D::Texture>> text_textures;
        Eng3D::State& s;
    public:
        TextureManager() = delete;
        TextureManager(Eng3D::State& s);
        ~TextureManager();
        std::shared_ptr<Eng3D::Texture> load(const std::string& path, TextureOptions options = default_options);
        std::shared_ptr<Eng3D::Texture> load(std::shared_ptr<Eng3D::IO::Asset::Base> asset, TextureOptions options = default_options);
        std::shared_ptr<Eng3D::Texture> gen_text(Eng3D::TrueType::Font& font, Eng3D::Color color, const std::string& msg);
        std::shared_ptr<Eng3D::Texture> get_white();
        void upload();

        friend class Eng3D::Texture;
    };
};
