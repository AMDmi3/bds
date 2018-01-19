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
#ifndef __WORLD__
#define __WORLD__

#include <cmath>
#include <cstdint>
#include <game/cgrid.h>
#include <game/drones.h>
#include <game/drops.h>
#include <game/load_state.h>
#include <game/particle.h>
#include <game/player.h>
#include <game/projectiles.h>
#include <game/sky.h>
#include <game/sound.h>
#include <game/static_instance.h>
#include <game/terrain.h>
#include <game/uniforms.h>
#include <min/camera.h>
#include <min/grid.h>
#include <min/physics_nt.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace game
{

class world
{
  private:
    static constexpr float _body_size = 21;
    static constexpr float _damping = 0.1;
    static constexpr float _time_step = 1.0 / 180.0;
    static constexpr float _grav_mag = 10.0;
    static constexpr size_t _pre_max_scale = 5;
    static constexpr size_t _pre_max_vol = _pre_max_scale * _pre_max_scale * _pre_max_scale;
    static constexpr size_t _ray_max_dist = 100;
    static constexpr float _explosion_radius = 4.0;

    // Terrain stuff
    cgrid _grid;
    game::terrain _terrain;
    particle *_particles;
    sound *_sound;
    std::vector<size_t> _view_chunks;

    // Physics stuff
    min::vec3<float> _gravity;
    min::physics<float, uint16_t, uint32_t, min::vec3, min::aabbox, min::aabbox, min::grid> _simulation;
    player _player;
    size_t _char_id;

    // Player control stuff
    min::mesh<float, uint32_t> _terr_mesh;
    min::vec3<int> _cached_offset;
    min::vec3<int> _preview_offset;
    min::vec3<float> _preview;
    min::vec3<unsigned> _scale;
    bool _edit_mode;

    // Skybox
    sky _sky;

    // Static instances for drones and drops
    static_instance _instance;
    drones _drones;
    drops _drops;
    projectiles _projectile;

    static inline min::vec3<float> center_radius(const min::vec3<float> &p, const min::vec3<unsigned> &scale)
    {
        const min::vec3<float> offset(scale.x() / 2, scale.y() / 2, scale.z() / 2);
        const min::vec3<float> center = p - offset;

        // return center position
        return center;
    }
    inline size_t character_load(const load_state &state)
    {
        const min::vec3<float> &p = state.get_spawn();

        // Create the physics body
        _char_id = _simulation.add_body(cgrid::player_box(p), 10.0);

        // Update recent chunk
        _grid.update_current_chunk(p);

        // If this the first time we load
        if (!state.is_loaded())
        {
            // Set scale to 3x3x3
            _scale = min::vec3<unsigned>(3, 3, 3);

            // Snap to grid
            const min::vec3<float> snapped = cgrid::snap(p);

            // Remove geometry around player, requires position snapped to grid and calculated direction vector
            _grid.set_geometry(snapped, _scale, _preview_offset, -1);

            // Reset scale to default value
            _scale = min::vec3<unsigned>(1, 1, 1);
        }

        return _char_id;
    }
    inline void explode_block(const min::vec3<float> &point, const min::vec3<float> &direction, const min::vec3<unsigned> &scale, const int8_t value, const float size = 100.0)
    {
        // Try to remove geometry from the grid
        const unsigned removed = _grid.set_geometry(point, scale, _preview_offset, -1);

        // If we removed geometry, play particles
        if (removed > 0)
        {
            // Add a drop
            _drops.add(point, direction, value);

            // Add particle effects
            _particles->load_static_explode(point, direction, 5.0, size);

            // Get player position
            const min::vec3<float> &p = _player.position();

            // Calculate distance from explosion center
            const float d = (point - p).magnitude();

            // Check if character is too close to the explosion
            const bool in_range = d < _explosion_radius;

            // If block is lava, play exploding sound
            // Prefer stereo if close to the explosion
            float power = 1000.0;
            if (in_range && value == 21)
            {
                // Play explode sound
                power = 5000.0;
                _sound->play_explode_stereo(point);
            }
            else if (value == 21)
            {
                // Play explode sound
                _sound->play_explode_mono(point);
            }

            // If explode hasn't been flagged yet
            if (!_player.is_exploded() && in_range)
            {
                _player.explode(direction, power, value);
            }
        }
    }
    // Generates the preview geometry and adds it to preview buffer
    inline void generate_preview()
    {
        // Lock in the preview offset
        _preview_offset = _cached_offset;

        // Load data into mesh
        _grid.atlas_preview(_terr_mesh, _preview_offset, _scale);

        // Upload preview geometry
        _terrain.upload_preview(_terr_mesh);
    }
    inline void reserve_memory(const size_t view_chunk_size)
    {
        // Reserve space in the simulation
        _simulation.reserve(_body_size);

        // Reserve space in the preview mesh
        _terr_mesh.vertex.reserve(_pre_max_vol);
        _terr_mesh.index.reserve(_pre_max_vol);

        // Reserve space for view chunks
        _view_chunks.reserve(view_chunk_size * view_chunk_size * view_chunk_size);
    }
    inline void set_collision_callbacks()
    {
        // Player collision callback
        const auto f = [this](min::body<float, min::vec3> &b1, min::body<float, min::vec3> &b2) {

            // Get other body id, b1 is player body
            const size_t id = b2.get_id();

            switch (id)
            {
            case 1:
                // Player collided with a drone
                _player.drone_collide(b2.get_position());
                break;
            case 2:
                break;
            default:
                break;
            }
        };

        // Register player collision callback
        _simulation.register_callback(_char_id, f);
    }
    inline void update_all_chunks()
    {
        // For all chunk meshes
        const size_t size = _grid.get_chunk_size();
        for (size_t i = 0; i < size; i++)
        {
            // If the chunk needs updating
            if (_grid.is_update_chunk(i))
            {
                // Upload contents to the vertex buffer
                _terrain.upload_geometry(i, _grid.get_chunk(i));

                // Flag that we updated the chunk
                _grid.update_chunk(i);
            }
        }
    }
    inline void update_world_physics(const float dt)
    {
        // Friction Coefficient
        const size_t steps = std::round(dt / _time_step);
        const float friction = -10.0 / steps;

        // Solve the physics simulation
        const min::vec3<float> &p = _player.position();

        // Explosion callback
        const auto ex = [this, p](const std::pair<min::aabbox<float, min::vec3>, int8_t> &cell) {

            // Check for exploding mines
            if (cell.second == 21)
            {
                // Calculate position and direction
                const min::vec3<unsigned> radius(3, 3, 3);
                const min::vec3<float> box_center = center_radius(cell.first.get_center(), radius);
                const min::vec3<float> dir = (p - box_center).normalize();

                // Explode the block with radius
                explode_block(box_center, dir, radius, cell.second, 100.0);
            }
        };

        // Send drones after the player
        _drones.set_destination(p);

        // Solve all physics timesteps
        for (size_t i = 0; i < steps; i++)
        {
            // Update the player on this frame
            _player.update_frame(_grid, friction, ex);

            // Update drones on this frame
            _drones.update_frame(_grid, _time_step);

            // Update drops on this frame
            _drops.update_frame(_grid, _time_step);

            // Solve all collisions
            _simulation.solve(_time_step, _damping);
        }

        // Update the drones positions
        _drones.update(_grid);

        // Update the drones positions
        _drops.update(_grid);

        // On missile collision remove block
        const auto on_collide = [this](const min::vec3<float> &point,
                                       const min::vec3<float> &direction,
                                       const min::vec3<unsigned> &scale, const int8_t value) {
            // Play missile explosion sound
            this->_sound->play_miss_ex(point);

            // Explode the block
            this->explode_block(point, direction, scale, value, 100.0);
        };

        // Update any missiles
        _projectile.update(_grid, steps * 0.0666, on_collide);
    }

  public:
    world(const load_state &state, particle *const particles, sound *const s, const game::uniforms &uniforms,
          const size_t chunk_size, const size_t grid_size, const size_t view_chunk_size)
        : _grid(chunk_size, grid_size, view_chunk_size),
          _terrain(_grid.get_chunk_size()),
          _particles(particles),
          _sound(s),
          _gravity(0.0, -_grav_mag, 0.0),
          _simulation(_grid.get_world(), _gravity),
          _player(&_simulation, character_load(state)),
          _terr_mesh("atlas"),
          _cached_offset(1, 1, 1),
          _preview_offset(1, 1, 1),
          _scale(1, 1, 1),
          _edit_mode(false),
          _sky(uniforms, grid_size),
          _instance(uniforms),
          _drones(&_simulation, &_instance, state.get_spawn()),
          _drops(&_simulation, &_instance),
          _projectile(particles, &_instance, s)
    {
        // Set the collision elasticity of the physics simulation
        _simulation.set_elasticity(0.1);

        // Set collision callbacks
        set_collision_callbacks();

        // Load the uniform buffer with program we will use
        uniforms.set_program(_terrain.get_program());

        // Reserve space for used vectors
        reserve_memory(view_chunk_size);

        // Update chunks
        update_all_chunks();
    }
    inline void add_block(const min::ray<float, min::vec3> &r)
    {
        // Trace a ray to the destination point to find placement position, return point is snapped
        const min::vec3<float> traced = _grid.ray_trace_prev(r, 6);

        // Add to grid
        _grid.set_geometry(traced, _scale, _preview_offset, _grid.get_atlas());
    }
    void draw(game::uniforms &uniforms) const
    {
        // Binds textures and uses program
        _terrain.bind();

        // Draw the world geometry
        _terrain.draw_terrain(_view_chunks);

        // Only draw if toggled
        if (_edit_mode)
        {
            // Set uniforms to light1
            uniforms.set_light2();

            // Draw the placemark
            _terrain.draw_placemark();

            // Set uniforms to light1
            uniforms.set_light1();
        }

        // Draw the sky, uses geometry VAO -- HACK!
        _sky.draw();

        // Draw the static instances
        _instance.draw(uniforms);

        // Draw the explode particles
        _particles->draw_static_explode(uniforms);

        // Draw projectiles
        _projectile.draw(uniforms);
    }
    int8_t explode_block(const min::ray<float, min::vec3> &r, const min::vec3<unsigned> &scale,
                         const std::function<void(const min::vec3<float> &, min::body<float, min::vec3> &)> &f = nullptr, const float size = 100.0)
    {
        // Trace a ray to the destination point to find placement position, return point is snapped
        int8_t value = -2;
        const min::vec3<float> traced = _grid.ray_trace_last(r, _ray_max_dist, value);

        // Get the atlas of target block, if hit a block remove it
        if (value >= 0)
        {
            // Get direction for particle spray
            const min::vec3<float> direction = r.get_direction() * -1.0;

            // Invoke the function callback if provided
            if (f)
            {
                // Get character body
                min::body<float, min::vec3> &body = _simulation.get_body(_char_id);

                // Call function callback
                f(traced, body);
            }

            // Remove the block
            const min::vec3<float> center = center_radius(traced, scale);

            // Explode block
            explode_block(center, direction, scale, value, size);
        }

        // return the block atlas id
        return value;
    }
    inline int8_t explode_block(const min::ray<float, min::vec3> &r,
                                const std::function<void(const min::vec3<float> &, min::body<float, min::vec3> &)> &f = nullptr, const float size = 100.0)
    {
        return explode_block(r, _scale, f, size);
    }
    inline uint8_t get_atlas_id() const
    {
        return _grid.get_atlas();
    }
    inline bool get_edit_mode()
    {
        return _edit_mode;
    }
    inline const cgrid &get_grid() const
    {
        return _grid;
    }
    inline const static_instance &get_instances() const
    {
        return _instance;
    }
    inline player &get_player()
    {
        return _player;
    }
    inline const player &get_player() const
    {
        return _player;
    }
    inline drones &get_drones()
    {
        return _drones;
    }
    inline const drones &get_drones() const
    {
        return _drones;
    }
    inline const drops &get_drops() const
    {
        return _drops;
    }
    inline const min::vec3<float> &get_preview_position()
    {
        return _preview;
    }
    bool hook_set(const min::ray<float, min::vec3> &r, min::vec3<float> &out)
    {
        // Trace a ray to the destination point to find placement position, return point is snapped
        int8_t value = -2;
        out = _grid.ray_trace_last(r, _ray_max_dist, value);

        // Get the atlas of target block, if hit a block remove it
        if (value >= 0)
        {
            // Calculate the hook length
            const float hook_length = (out - r.get_origin()).magnitude();

            // Set player hook
            _player.set_hook(out, hook_length);

            // Return that we are hooked
            return true;
        }

        // Return that we are  not hooked
        return false;
    }
    inline bool launch_missile(const min::ray<float, min::vec3> &r)
    {
        // Launch a missile on this ray
        return _projectile.launch_missile(_grid, r);
    }
    inline void reset_explode()
    {
        _player.reset_explode();
    }
    inline void respawn(const min::vec3<float> p)
    {
        // Respawn player
        _player.respawn();

        // Set character position
        _player.warp(p);

        // Zero out character velocity
        _player.velocity(min::vec3<float>());
    }
    inline void reset_scale()
    {
        // Reset the scale and the cached offset
        _scale = min::vec3<unsigned>(1, 1, 1);
        _cached_offset = min::vec3<int>(1, 1, 1);

        // Only applicable in edit mode
        if (_edit_mode)
        {
            // Regenerate the preview mesh
            generate_preview();
        }
        else
        {
            _preview_offset = _cached_offset;
        }
    }
    inline int8_t scan_block(const min::ray<float, min::vec3> &r)
    {
        // Trace a ray to the destination point to find placement position, return point is snapped
        int8_t value = -2;
        _grid.ray_trace_last(r, _ray_max_dist, value);

        // return the block id
        return value;
    }
    inline void set_atlas_id(const int8_t id)
    {
        // Only applicable in edit mode
        if (_edit_mode)
        {
            _grid.set_atlas(id);

            // Regenerate the preview mesh
            generate_preview();
        }
    }
    inline void set_scale_x(unsigned dx)
    {
        // Only applicable in edit mode
        if (_edit_mode)
        {
            if (_cached_offset.x() != _preview_offset.x())
            {
                // Regenerate the preview mesh
                generate_preview();
            }
            else if (_scale.x() < _pre_max_scale)
            {
                _scale.x(_scale.x() + dx);

                // Regenerate the preview mesh
                generate_preview();
            }
        }
    }
    inline void set_scale_y(unsigned dy)
    {
        // Only applicable in edit mode
        if (_edit_mode)
        {
            if (_cached_offset.y() != _preview_offset.y())
            {
                // Regenerate the preview mesh
                generate_preview();
            }
            else if (_scale.y() < _pre_max_scale)
            {
                _scale.y(_scale.y() + dy);

                // Regenerate the preview mesh
                generate_preview();
            }
        }
    }
    inline void set_scale_z(unsigned dz)
    {
        // Only applicable in edit mode
        if (_edit_mode)
        {
            if (_cached_offset.z() != _preview_offset.z())
            {
                // Regenerate the preview mesh
                generate_preview();
            }
            else if (_scale.z() < _pre_max_scale)
            {
                _scale.z(_scale.z() + dz);

                // Regenerate the preview mesh
                generate_preview();
            }
        }
    }
    inline bool target_block(const min::ray<float, min::vec3> &r, min::vec3<float> &out)
    {
        // Trace a ray to the destination point to find placement position, return point is snapped
        int8_t value = -2;
        out = _grid.ray_trace_last(r, _ray_max_dist, value);

        // return if we hit something
        return value >= 0;
    }
    inline bool toggle_edit_mode()
    {
        // Toggle flag and return result
        return (_edit_mode = !_edit_mode);
    }
    void update(min::camera<float> &cam, const float dt)
    {
        // Update the physics and AI in world
        update_world_physics(dt);

        // Get player position
        const min::vec3<float> &p = _player.position();

        // Detect if we crossed a chunk boundary
        _grid.update_current_chunk(p);

        // Get surrounding chunks for drawing
        _grid.get_view_chunks(cam, _view_chunks);

        // For all chunk meshes
        for (const auto &i : _view_chunks)
        {
            // If the chunk needs updating
            if (_grid.is_update_chunk(i))
            {
                // Upload contents to the vertex buffer
                _terrain.upload_geometry(i, _grid.get_chunk(i));

                // Flag that we updated the chunk
                _grid.update_chunk(i);
            }
        }

        // Calculate new placemark point
        const min::vec3<float> dest = cam.project_point(3.0);

        // Create a ray from camera to destination
        const min::ray<float, min::vec3> r(cam.get_position(), dest);

        // Trace a ray to the destination point to find placement position, return point is snapped
        _preview = _grid.ray_trace_prev(r, 6);

        // Update offset x-vector
        if (cam.get_forward().x() >= 0.0)
        {
            _cached_offset.x(1);
        }
        else
        {
            _cached_offset.x(-1);
        }

        // Update offset z-vector
        if (cam.get_forward().z() >= 0.0)
        {
            _cached_offset.z(1);
        }
        else
        {
            _cached_offset.z(-1);
        }
    }
};
}

#endif
