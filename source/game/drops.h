/* Copyright [2013-2018] [Aaron Springstroh, Minimal Graphics Library]

This file is part of the Beyond Dying Skies.

Beyond Dying Skies is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Beyond Dying Skies is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Beyond Dying Skies.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef __DROPS__
#define __DROPS__

#include <game/static_instance.h>
#include <min/aabbox.h>
#include <min/grid.h>
#include <min/physics_nt.h>
#include <min/vec3.h>
#include <vector>

namespace game
{
class drop
{
  private:
    size_t _body_id;
    size_t _inst_id;
    int8_t _atlas;

  public:
    drop(const size_t body_id, const size_t inst_id, const int8_t atlas)
        : _body_id(body_id), _inst_id(inst_id), _atlas(atlas) {}

    inline int8_t atlas() const
    {
        return _atlas;
    }
    inline size_t body_id() const
    {
        return _body_id;
    }
    inline void dec_inst()
    {
        _inst_id--;
    }
    inline size_t inst_id() const
    {
        return _inst_id;
    }
};

class drops
{
  private:
    static constexpr float _rotation_rate = 2.0;
    typedef min::physics<float, uint16_t, uint32_t, min::vec3, min::aabbox, min::aabbox, min::grid> physics;
    physics *_sim;
    static_instance *_inst;
    std::vector<min::aabbox<float, min::vec3>> _col_cells;
    std::vector<drop> _drops;
    float _angle;
    size_t _oldest;

    inline min::body<float, min::vec3> &body(const size_t index)
    {
        return _sim->get_body(_drops[index].body_id());
    }
    inline const min::body<float, min::vec3> &body(const size_t index) const
    {
        return _sim->get_body(_drops[index].body_id());
    }
    inline void force(const size_t index, const min::vec3<float> &f)
    {
        // Get the drop body
        min::body<float, min::vec3> &b = body(index);

        // Apply force to the body per mass
        b.add_force(f * b.get_mass());
    }
    inline float mass(const size_t index) const
    {
        return body(index).get_mass();
    }
    inline const min::vec3<float> &position(const size_t index) const
    {
        // Return the drop position
        return body(index).get_position();
    }
    inline void reserve_memory()
    {
        // Reserve space for collision cells
        _col_cells.reserve(27);
        _drops.reserve(static_instance::max_drops());
    }
    inline const min::vec3<float> &velocity(const size_t index) const
    {
        // Return the drop velocity
        return body(index).get_linear_velocity();
    }

  public:
    drops(physics *sim, static_instance *inst)
        : _sim(sim), _inst(inst), _angle(0.0), _oldest(0)
    {
        reserve_memory();
    }
    inline size_t add(const min::vec3<float> &p, const min::vec3<float> &dir, const int8_t atlas)
    {
        // If all boxes have been allocated, cannibalize
        if (_inst->drop_full())
        {
            // Get oldest index to consume
            const size_t max_drop = static_instance::max_drops();
            const size_t index = (_oldest %= max_drop)++;

            // Get the old ID's
            const size_t inst_id = _drops[index].inst_id();
            const size_t body_id = _drops[index].body_id();

            // Get the physics body for editing
            min::body<float, min::vec3> &body = _sim->get_body(body_id);

            // Set body linear velocity
            const min::vec3<float> lv = min::vec3<float>(0.0, 5.0, 0.0) + dir * -5.0;
            body.set_linear_velocity(lv);

            // Update body position
            _inst->update_drop_position(inst_id, p);
            _inst->update_drop_atlas(inst_id, atlas);
            body.set_position(p);

            // Store the drop index as body data
            body.set_data(min::body_data(index));

            // Recreate drop
            _drops[index] = drop(body_id, inst_id, atlas);

            // Return the drop index
            return index;
        }

        // Create a drop instance
        const size_t inst_id = _inst->add_drop(p, atlas);

        // Create a box for the drop
        const min::aabbox<float, min::vec3> box = _inst->box_drop(inst_id);

        // Store the drop index as body data
        const size_t index = _drops.size();

        // Add to physics simulation
        const size_t body_id = _sim->add_body(box, 10.0, 2, index);

        // Get the physics body for editing
        min::body<float, min::vec3> &body = _sim->get_body(body_id);

        // Set body linear velocity
        const min::vec3<float> lv = min::vec3<float>(0.0, 5.0, 0.0) + dir * -5.0;
        body.set_linear_velocity(lv);

        // Create a new drop
        _drops.emplace_back(body_id, inst_id, atlas);

        // Return the drop index
        return _drops.size() - 1;
    }
    inline int8_t atlas(const size_t index) const
    {
        return _drops[index].atlas();
    }
    inline void remove(const size_t index)
    {
        // Clear drop at index
        _inst->clear_drop(_drops[index].inst_id());
        _sim->clear_body(_drops[index].body_id());
        _drops.erase(_drops.begin() + index);

        // Adjust the remaining drop indices
        const size_t size = _drops.size();
        for (size_t i = index; i < size; i++)
        {
            // Adjust the instance id
            _drops[i].dec_inst();

            // Adjust the body data index
            body(i).set_data(min::body_data(i));
        }
    }
    inline void update_frame(const cgrid &grid, const float friction)
    {
        // Do drop collisions
        const size_t size = _drops.size();
        for (size_t i = 0; i < size; i++)
        {
            // Get all cells that could collide
            grid.drop_collision_cells(_col_cells, position(i));

            // Solve static collisions
            const size_t body = _drops[i].body_id();
            for (const auto &cell : _col_cells)
            {
                _sim->collide(body, cell);
            }

            // Add friction force
            const min::vec3<float> &vel = velocity(i);
            const min::vec3<float> xz(vel.x(), 0.0, vel.z());

            // Add friction force opposing lateral motion
            force(i, xz * friction);
        }
    }
    inline void update(const cgrid &grid)
    {
        // Update the drop rotation angle
        _angle += _rotation_rate;
        if (_angle > 360.0)
        {
            _angle -= 360.0;
        }

        // Calculate quaternion around Y axis
        const min::quat<float> q(min::vec3<float>::up(), _angle);

        // Update all drop positions
        const size_t size = _drops.size();
        for (size_t i = 0; i < size; i++)
        {
            // Update the instance matrix
            const size_t inst_id = _drops[i].inst_id();

            // Update body positions
            const min::vec3<float> &p = body(i).get_position();
            _inst->update_drop_position(inst_id, p);

            // Update body rotations
            _inst->update_drop_rotation(inst_id, q);
        }
    }
};
}

#endif
