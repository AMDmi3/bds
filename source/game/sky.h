/* Copyright [2013-2018] [Aaron Springstroh, Minimal Graphics Library]

This file is part of the Fractex.

Fractex is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fractex is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fractex.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef __SKY__
#define __SKY__

#include <game/memory_map.h>
#include <min/dds.h>
#include <min/static_vertex.h>
#include <min/vertex_buffer.h>

namespace game
{

class sky
{
  private:
    // OpenGL stuff
    min::shader _sv;
    min::shader _sf;
    min::program _prog;

    // Texture stuff
    min::texture_buffer _tbuffer;
    GLuint _dds_id;

  public:
    sky(const game::uniforms &uniforms, const float extent)
        : _sv(memory_map::memory.get_file("data/shader/sky.vertex"), GL_VERTEX_SHADER),
          _sf(memory_map::memory.get_file("data/shader/sky.fragment"), GL_FRAGMENT_SHADER),
          _prog(_sv, _sf)
    {
        // Let this program use this uniform buffer
        uniforms.set_program_lights(_prog);
        uniforms.set_program_matrix(_prog);

        // Load texture
        const min::mem_file &sky = memory_map::memory.get_file("data/texture/sky.dds");
        min::dds tex(sky);

        // Load texture buffer
        _dds_id = _tbuffer.add_dds_texture(tex);
    }
    void draw() const
    {
        // Bind this texture for drawing
        _tbuffer.bind(_dds_id, 0);

        // Use the sky program
        _prog.use();

        // Draw texture to background
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
};
}

#endif
