// Symphony of Empires
// Copyright (C) 2021, Symphony of Empires contributors
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
//      eng3d/ui/chart.cpp
//
// Abstract:
//      Does some important stuff.
// ----------------------------------------------------------------------------

#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>

#include <glm/vec2.hpp>

#include "eng3d/ui/widget.hpp"
#include "eng3d/ui/ui.hpp"
#include "eng3d/ui/chart.hpp"
#include "eng3d/texture.hpp"
#include "eng3d/rectangle.hpp"
#include "eng3d/state.hpp"

UI::Chart::Chart(int _x, int _y, unsigned w, unsigned h, UI::Widget* _parent)
    : UI::Widget(_parent, _x, _y, w, h, UI::WidgetType::LABEL)
{

}

void UI::Chart::on_render(UI::Context& ui_ctx, Eng3D::Rect viewport) {
    ui_ctx.obj_shader->set_uniform("diffuse_color", glm::vec4(1.f));
    if(current_texture != nullptr)
        draw_rectangle(0, 0, width, height, viewport, current_texture.get());

    if(text_texture.get() != nullptr) {
        ui_ctx.obj_shader->set_uniform("diffuse_color", glm::vec4(text_color.r, text_color.g, text_color.b, 1.f));
        draw_rectangle(4, 2, text_texture->width, text_texture->height, viewport, text_texture.get());
    }

    /// @todo Data pops shouldn't be done here
    if(data.size() >= 30)
        data.pop_back();
}