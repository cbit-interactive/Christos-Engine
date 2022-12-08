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
//      texture.cpp
//
// Abstract:
//      Defines some functions to bind a BinaryImage to an OpenGL object
//      by using the OpenGL API for managing the resources of it.
// ----------------------------------------------------------------------------

#include <algorithm>
#include <cassert>
#include <SDL_ttf.h>

#ifdef E3D_BACKEND_OPENGL
#   include <GL/glew.h>
#   include <GL/gl.h>
#elif defined E3D_BACKEND_GLES
#   include <GLES3/gl3.h>
#endif

#include "eng3d/texture.hpp"
#include "eng3d/framebuffer.hpp"
#include "eng3d/utils.hpp"
#include "eng3d/state.hpp"
#include "eng3d/log.hpp"

//
// Texture
//
Eng3D::Texture::Texture(const std::string& path)
    : Eng3D::BinaryImage(path)
{

}

Eng3D::Texture::Texture(const Eng3D::IO::Asset::Base* asset)
    : Eng3D::BinaryImage((asset == nullptr) ? "" : asset->abs_path)
{

}

Eng3D::Texture::Texture(size_t _width, size_t _height, size_t _bpp)
    : Eng3D::BinaryImage(_width, _height, _bpp)
{

}

Eng3D::Texture::~Texture() {
#if defined E3D_BACKEND_OPENGL || defined E3D_BACKEND_GLES
    if(id)
        delete_gputex();
#endif
    if(managed) {
        auto& s = Eng3D::State::get_instance();
        const std::scoped_lock lock(s.tex_man.unuploaded_lock);
        auto it = std::find_if(s.tex_man.unuploaded_textures.begin(), s.tex_man.unuploaded_textures.end(), [this](const auto& e) {
            return e.texture == this;
        });
        if(it != s.tex_man.unuploaded_textures.end())
            s.tex_man.unuploaded_textures.erase(it);
    }
}

/// @brief This dummy texture helps to avoid crashes due to missing buffers or so, and also gives
/// visual aid of errors
void Eng3D::Texture::create_dummy() {
    width = 8;
    height = 8;
    buffer = std::make_unique<uint32_t[]>(width * height);
    if(buffer.get() == nullptr)
        CXX_THROW(TextureException, "Dummy", "Out of memory for dummy texture");

    // Fill in with a permutation pattern of pink and black
    // This should be autovectorized by gcc
    for(size_t i = 0; i < width * height; i++)
        buffer.get()[i] = ((i * 8) << 16) | (i * 16);
}

/// @brief Frontend for uploading (schedules or instantly uploads)
/// @param options Options for upload
void Eng3D::Texture::upload(TextureOptions options) {
    auto& s = Eng3D::State::get_instance();
    if(options.instant_upload || !managed) {
        this->_upload(options); // Do upload instantly
    } else {
        if(managed) {
            TextureUploadRequest request{};
            request.texture = this;
            request.options = options;

            const std::scoped_lock lock(s.tex_man.unuploaded_lock);
            s.tex_man.unuploaded_textures.push_back(request); // Schedule upload for later
        }
    }
}

/// @brief Uploads a text texture (shceduled or not) if it's scheduled, the surface
/// is handed ownership over to the scheduler and it will be automatically deallocated
/// once the request is fullfilled
/// @param surface Surface to base texture from
void Eng3D::Texture::upload(SDL_Surface* surface) {
    auto& s = Eng3D::State::get_instance();
    if(!managed) {
        this->_upload(surface);
    } else {
        if(managed) {
            TextureUploadRequest request{};
            request.texture = this;
            request.surface = surface;

            const std::scoped_lock lock(s.tex_man.unuploaded_lock);
            s.tex_man.unuploaded_textures.push_back(request);
        }
    }
}

void Eng3D::Texture::_upload(TextureOptions options) {
#if defined E3D_BACKEND_OPENGL || defined E3D_BACKEND_GLES
    if(id) delete_gputex();

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

#ifndef E3D_BACKEND_GLES
    GLuint internal_format = 0;
    switch(options.internal_format) {
    case Eng3D::TextureOptions::Format::RGBA:
        internal_format = GL_RGBA;
        break;
    case Eng3D::TextureOptions::Format::RED:
        internal_format = GL_RED;
        break;
    case Eng3D::TextureOptions::Format::SRGB:
        internal_format = GL_SRGB;
        break;
    case Eng3D::TextureOptions::Format::RGB32F:
        internal_format = GL_RGB32F;
        break;
    case Eng3D::TextureOptions::Format::SRGB_ALPHA:
        internal_format = GL_SRGB_ALPHA;
        break;
    default:
        break;
    }

    /// @todo This causes a lot of issues!
    // Compress the texture if it can't be edited, this is only available on normal OpenGL through
    if(!options.editable && options.compressed) {
        switch(internal_format) {
        case GL_ALPHA:
            internal_format = GL_COMPRESSED_ALPHA;
            break;
        case GL_LUMINANCE:
            internal_format = GL_COMPRESSED_LUMINANCE;
            break;
        case GL_LUMINANCE_ALPHA:
            internal_format = GL_COMPRESSED_LUMINANCE_ALPHA;
            break;
        case GL_INTENSITY:
            internal_format = GL_COMPRESSED_INTENSITY;
            break;
        case GL_RED:
            internal_format = GL_COMPRESSED_RED;
            break;
        case GL_RGB:
            internal_format = GL_COMPRESSED_RGB;
            break;
        case GL_RGBA:
            internal_format = GL_COMPRESSED_RGBA;
            break;
        case GL_SRGB:
            internal_format = GL_COMPRESSED_SRGB;
            break;
        case GL_SRGB_ALPHA:
            internal_format = GL_COMPRESSED_SRGB_ALPHA;
            break;
        case GL_RG:
            internal_format = GL_COMPRESSED_RG;
            break;
        default:
            break;
        }
    }
#endif

    GLuint format = 0;
    switch(options.format) {
    case Eng3D::TextureOptions::Format::RGBA:
        format = GL_RGBA;
        break;
    case Eng3D::TextureOptions::Format::RED:
        format = GL_RED;
        break;
    case Eng3D::TextureOptions::Format::SRGB:
        format = GL_SRGB;
        break;
    case Eng3D::TextureOptions::Format::RGB32F:
        format = GL_RGB32F;
        break;
    case Eng3D::TextureOptions::Format::SRGB_ALPHA:
        format = GL_SRGB_ALPHA;
        break;
    default:
        break;
    }

    GLuint type = 0;
    switch(options.type) {
    case Eng3D::TextureOptions::Type::UNSIGNED_BYTE:
        type = GL_UNSIGNED_BYTE;
        break;
    default:
        break;
    }

    GLuint wrap_s = 0;
    switch(options.wrap_s) {
    case Eng3D::TextureOptions::Wrap::REPEAT:
        wrap_s = GL_REPEAT;
        break;
    case Eng3D::TextureOptions::Wrap::CLAMP_TO_EDGE:
        wrap_s = GL_CLAMP_TO_EDGE;
        break;
    default:
        break;
    }

    GLuint wrap_t = 0;
    switch(options.wrap_t) {
    case Eng3D::TextureOptions::Wrap::REPEAT:
        wrap_t = GL_REPEAT;
        break;
    case Eng3D::TextureOptions::Wrap::CLAMP_TO_EDGE:
        wrap_t = GL_CLAMP_TO_EDGE;
        break;
    default:
        break;
    }

    GLuint min_filter = 0;
    switch(options.min_filter) {
    case Eng3D::TextureOptions::Filter::NEAREST:
        min_filter = GL_NEAREST;
        break;
    case Eng3D::TextureOptions::Filter::LINEAR:
        min_filter = GL_LINEAR;
        break;
    case Eng3D::TextureOptions::Filter::LINEAR_MIPMAP:
        min_filter = GL_LINEAR_MIPMAP_LINEAR;
        break;
    case Eng3D::TextureOptions::Filter::NEAREST_MIPMAP:
        min_filter = GL_NEAREST_MIPMAP_NEAREST;
        break;
    case Eng3D::TextureOptions::Filter::NEAREST_LINEAR_MIPMAP:
        min_filter = GL_NEAREST_MIPMAP_LINEAR;
        break;
    case Eng3D::TextureOptions::Filter::LINEAR_NEAREST_MIPMAP:
        min_filter = GL_LINEAR_MIPMAP_NEAREST;
        break;
    default:
        break;
    }

    GLuint mag_filter = 0;
    switch(options.mag_filter) {
    case Eng3D::TextureOptions::Filter::NEAREST:
        mag_filter = GL_NEAREST;
        break;
    case Eng3D::TextureOptions::Filter::LINEAR:
        mag_filter = GL_LINEAR;
        break;
    case Eng3D::TextureOptions::Filter::LINEAR_MIPMAP:
        mag_filter = GL_LINEAR_MIPMAP_LINEAR;
        break;
    case Eng3D::TextureOptions::Filter::NEAREST_MIPMAP:
        mag_filter = GL_NEAREST_MIPMAP_NEAREST;
        break;
    case Eng3D::TextureOptions::Filter::NEAREST_LINEAR_MIPMAP:
        mag_filter = GL_NEAREST_MIPMAP_LINEAR;
        break;
    case Eng3D::TextureOptions::Filter::LINEAR_NEAREST_MIPMAP:
        mag_filter = GL_LINEAR_MIPMAP_NEAREST;
        break;
    default:
        break;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, buffer.get());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);

    if(!options.editable) {
#ifdef E3D_DEBUG
        GLint result;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &result);
        if(result == 0) {
            Eng3D::Log::debug("opengl", "Couldn't compress texture of " + std::to_string(width) + "x" + std::to_string(height));
        } else {
            Eng3D::Log::debug("opengl", "Compressed texture of " + std::to_string(width) + "x" + std::to_string(height));
        }
#endif
        // We will free up the texture if we don't plan on editing it since it's on the GPU now
        buffer.reset();
    }
#endif
    switch(options.min_filter) {
    case Eng3D::TextureOptions::Filter::LINEAR_MIPMAP:
    case Eng3D::TextureOptions::Filter::NEAREST_MIPMAP:
    case Eng3D::TextureOptions::Filter::NEAREST_LINEAR_MIPMAP:
    case Eng3D::TextureOptions::Filter::LINEAR_NEAREST_MIPMAP:
        this->gen_mipmaps();
        break;
    default:
        break;
    }
}

/// @brief Converts the texture into a OpenGL texture, and assigns it a number
void Eng3D::Texture::_upload(SDL_Surface* surface) {
    if(surface->w == 0 || surface->h == 0) return;
    assert(surface->format != nullptr);

    this->buffer.reset();
    this->width = static_cast<size_t>(surface->w);
    this->height = static_cast<size_t>(surface->h);

    int colors = surface->format->BytesPerPixel;
#if defined E3D_BACKEND_OPENGL || defined E3D_BACKEND_GLES
    GLuint texture_format;
    if(colors == 4) {
        // Alpha
        if(surface->format->Rmask == 0x000000ff) {
            texture_format = GL_RGBA;
        } else {
#ifdef GL_BGRA
            texture_format = GL_BGRA;
#else
            texture_format = GL_RGBA; //CXX_THROW(std::runtime_error, "Unsupported texture format");
#endif
        }
    } else {
        // No alpha
        if(surface->format->Rmask == 0x000000ff) {
            texture_format = GL_RGB;
        } else {
#ifdef GL_BGR
            texture_format = GL_BGR;
#else
            texture_format = GL_RGB; //CXX_THROW(std::runtime_error, "Unsupported texture format");
#endif
        }
    }

    int alignment = 8;
    while(surface->pitch % alignment)
        alignment >>= 1; // x%1==0 for any x
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

    int expected_pitch = (surface->w * surface->format->BytesPerPixel + alignment - 1) / alignment * alignment;
    if(surface->pitch - expected_pitch >= alignment) {
        // Alignment alone wont't solve it now
        glPixelStorei(GL_UNPACK_ROW_LENGTH, surface->pitch / surface->format->BytesPerPixel);
    } else {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, colors, surface->w, surface->h, 0, texture_format, GL_UNSIGNED_BYTE, surface->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    this->gen_mipmaps();
    SDL_FreeSurface(surface);
}

void Eng3D::Texture::gen_mipmaps() const {
#if defined E3D_BACKEND_OPENGL || defined E3D_BACKEND_GLES
    glBindTexture(GL_TEXTURE_2D, id);
    glGenerateMipmap(GL_TEXTURE_2D);
#endif
}

/// @brief Binds the texture to the current OpenGL context
void Eng3D::Texture::bind() const {
#if defined E3D_BACKEND_OPENGL || defined E3D_BACKEND_GLES
    glBindTexture(GL_TEXTURE_2D, id);
#endif
}

/// @brief Deletes the OpenGL representation of this texture
void Eng3D::Texture::delete_gputex() {
#if defined E3D_BACKEND_OPENGL || defined E3D_BACKEND_GLES
    glDeleteTextures(1, &id);
#endif
}

#define STB_IMAGE_WRITE_STATIC 1
#define STB_IMAGE_WRITE_IMPLEMENTATION 1
extern "C" {
#include "eng3d/stb_image_write.h"
}
void Eng3D::Texture::to_file(const std::string& filename) {
    int channel_count = 4;
    int stride = channel_count * width;
    int data_size = stride * height;

#if defined E3D_BACKEND_OPENGL
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, id);
#endif
    uint8_t* data = (uint8_t*)malloc(data_size);
#if defined E3D_BACKEND_OPENGL
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
#endif
    stbi_write_png(filename.c_str(), width, height, channel_count, data, stride);
}

//
// Texture array
//

/// @brief Creates a new texture array
Eng3D::TextureArray::TextureArray(const std::string& path, size_t _tiles_x, size_t _tiles_y)
    : Eng3D::BinaryImage(path),
    tiles_x{ _tiles_x },
    tiles_y{ _tiles_y }
{

}

/// @brief Uploads the TextureArray to the driver
void Eng3D::TextureArray::upload() {
#if defined E3D_BACKEND_OPENGL || defined E3D_BACKEND_GLES
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D_ARRAY, id);

    // set up texture handle parameters
    // glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0); // !single image!
    // glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 1); // !single image! mat->mips == 1
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    size_t p_dx = width / tiles_x; // pixels of each tile in x
    size_t p_dy = height / tiles_y; // pixels of each tile in y
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, p_dx, p_dy, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, height);

    for(size_t x = 0; x < tiles_x; x++)
        for(size_t y = 0; y < tiles_y; y++)
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, x * tiles_x + y, p_dx, p_dy, 1, GL_RGBA, GL_UNSIGNED_BYTE, buffer.get() + (x * p_dy * width + y * p_dx));
    //glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    // unbind texture handle
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
#endif
}

//
// Texture manager
//
Eng3D::TextureManager::TextureManager(Eng3D::State& _s)
    : s{ _s }
{

}

Eng3D::TextureManager::~TextureManager()
{
    const std::scoped_lock lock(this->unuploaded_lock);
    // Surfaces have to be properly deallocated before calling SDL_Quit()
    for(auto& request : this->unuploaded_textures) {
        if(request.surface != nullptr) {
            SDL_FreeSurface(request.surface);
            request.surface = nullptr;
        }
    }
}

std::shared_ptr<Eng3D::Texture> Eng3D::TextureManager::get_white() {
    if(white.get() == nullptr) {
        white = std::make_shared<Eng3D::Texture>(1, 1);
        white->buffer.get()[0] = 0xFFFFFFFF;
        white->upload();
    }
    return std::shared_ptr<Eng3D::Texture>(white);
}

/// @brief Finds a texture in the list of a texture manager if the texture is already in the
/// list we load the saved texture from the list instead of loading it from the disk. Otherwise
/// we load it from the disk and add it to the saved texture list. The object returned is a
/// pointer and we will not give ownership of textures in the list and program should not modify
/// the contents of it since it will differ from the texture on the disk, and our main point is to
/// mirror loaded textures from the disk - not modify them.
std::shared_ptr<Eng3D::Texture> Eng3D::TextureManager::load(const std::string& path, TextureOptions options) {
    // Find texture when wanting to be loaded and load texture from cached texture list
    auto key = std::make_pair(path, options);
    auto it = textures.find(key);
    if(it != textures.end()) return (*it).second;

    Eng3D::Log::debug("texture", "Loaded and cached texture " + path);

    // Otherwise texture is not in our control, so we create a new texture
    std::shared_ptr<Eng3D::Texture> tex;
    try {
        tex = std::make_shared<Eng3D::Texture>(path);
    } catch(const BinaryImageException&) {
        tex = std::make_shared<Eng3D::Texture>();
        tex->create_dummy();
    }
    tex->managed = true;
    tex->upload(options);
    textures[key] = tex;
    return textures[key];
}

std::shared_ptr<Eng3D::Texture> Eng3D::TextureManager::load(std::shared_ptr<Eng3D::IO::Asset::Base> asset, TextureOptions options) {
    return this->load(asset.get() != nullptr ? asset->get_abs_path() : "", options);
}

std::shared_ptr<Eng3D::Texture> Eng3D::TextureManager::gen_text(Eng3D::TrueType::Font& font, Eng3D::Color color, const std::string& msg) {
    if(msg.empty()) return this->get_white();

    // Find texture when wanting to be loaded and load texture from cached texture list
    auto it = text_textures.find(msg);
    if(it != text_textures.end()) return it->second;

    Eng3D::Log::debug("texture", "Loaded and cached text texture for " + msg);

    // Otherwise texture is not in our control, so we create a new texture
    auto tex = std::make_shared<Eng3D::Texture>();
    Eng3D::Log::debug("ttf", "Creating text for \"" + msg + "\"");
    const SDL_Color white_color{
        static_cast<Uint8>(color.r * 255.f), static_cast<Uint8>(color.g * 255.f),
        static_cast<Uint8>(color.b * 255.f), 0 };
    auto* surface = TTF_RenderUTF8_Blended(static_cast<TTF_Font*>(font.sdl_font), msg.c_str(), white_color);
    if(surface == nullptr)
        CXX_THROW(std::runtime_error, Eng3D::translate_format("Cannot create text surface: %s", TTF_GetError()));
    Eng3D::Log::debug("ttf", "Sucessfully created text");
    tex->managed = true;
    // Required so UI widgets can resize properly!
    tex->width = surface->w;
    tex->height = surface->h;
    tex->upload(surface);

    text_textures[msg] = tex;
    return text_textures[msg];
}

void Eng3D::TextureManager::upload() {
    if(!this->unuploaded_textures.empty()) {
        const std::scoped_lock lock(this->unuploaded_lock);
        auto it = this->unuploaded_textures.end() - 1;
        auto request = *it;
        if(request.surface != nullptr) {
            request.texture->_upload(request.surface);
            request.surface = nullptr;
        } else {
            request.texture->_upload(request.options);
        }
        this->unuploaded_textures.erase(it);
    }
}
