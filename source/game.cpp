/* Copyright [2013-2016] [Aaron Springstroh, Minimal Graphics Library]

This file is part of the MGLCraft.

MGLCraft is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

MGLCraft is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with MGLCraft.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <game/controls.h>
#include <game/file.h>
#include <game/goal_seek.h>
#include <game/particle.h>
#include <game/state.h>
#include <game/text.h>
#include <game/uniforms.h>
#include <game/world.h>
#include <iomanip>
#include <iostream>
#include <min/bmp.h>
#include <min/camera.h>
#include <min/loop_sync.h>
#include <min/settings.h>
#include <min/utility.h>
#include <min/window.h>
#include <sstream>
#include <string>
#include <utility>

class mglcraft
{
  private:
    min::window _win;

    // Game specific classes
    game::text _text;
    game::uniforms _uniforms;
    game::particle _particles;
    game::character _character;
    game::state _state;
    game::world _world;
    game::controls _controls;
    game::goal_seek _goal_seek;
    double _fps;
    double _idle;

    inline void load_text()
    {
        // Set the screen size
        _text.set_screen(720, 480);

        // Add test text
        _text.add_text("MGLCRAFT: Official Demo", 10, 460);

        // Add cross hairs
        _text.add_text("(X)", 346, 226);

        // Add character position
        _text.add_text("X: Y: Z:", 10, 432);

        // Add character direction
        _text.add_text("X: Y: Z:", 10, 404);

        // Game mode
        _text.add_text("MODE: PLAY:", 10, 376);

        // Destination
        _text.add_text("DEST:", 10, 348);

        // Energy
        _text.add_text("ENERGY:", 10, 320);

        // FPS
        _text.add_text("FPS:", 10, 292);

        // IDLE
        _text.add_text("IDLE:", 10, 264);
    }
    inline std::pair<min::vec3<float>, bool> load_state()
    {
        // Create output stream for loading world
        std::vector<uint8_t> stream;

        // Load data into stream from file
        game::load_file("bin/state", stream);

        // If load failed dont try to parse stream data
        if (stream.size() != 0)
        {
            // Character position
            size_t next = 0;
            const float x = min::read_le<float>(stream, next);
            const float y = min::read_le<float>(stream, next);
            const float z = min::read_le<float>(stream, next);

            // Load character at this position
            const min::vec3<float> p(x, y, z);

            // Look direction
            const float lx = min::read_le<float>(stream, next);
            const float ly = min::read_le<float>(stream, next);
            const float lz = min::read_le<float>(stream, next);

            // Load camera settings
            const min::vec3<float> look(lx, ly, lz);
            _state.set_camera(p, look);

            // return the state
            return std::make_pair(p, true);
        }
        else
        {
            // Load character at the default position
            const min::vec3<float> p(0.0, 31.0, 0.0);

            // Load camera settings
            const min::vec3<float> look(1.0, 31.0, 0.0);
            _state.set_camera(p, look);

            // return the state
            return std::make_pair(p, false);
        }
    }
    inline void save_state()
    {
        // Create output stream for saving world
        std::vector<uint8_t> stream;

        // Get character position
        const min::vec3<float> &p = _world.character_position();

        // Write position into stream
        min::write_le<float>(stream, p.x());
        min::write_le<float>(stream, p.y());
        min::write_le<float>(stream, p.z());

        // Get the camera look position
        const min::vec3<float> look = _state.get_camera().project_point(3.0);

        // Write look into stream
        min::write_le<float>(stream, look.x());
        min::write_le<float>(stream, look.y());
        min::write_le<float>(stream, look.z());

        // Write data to file
        game::save_file("bin/state", stream);
    }
    inline std::pair<unsigned, unsigned> user_input()
    {
        if (_state.get_user_input())
        {
            // Get the cursor coordinates
            const auto c = _win.get_cursor();

            // Reset cursor position
            update_cursor();

            // return the mouse coordinates
            return c;
        }

        // return no mouse movement
        return std::make_pair(_win.get_width() / 2, _win.get_height() / 2);
    }
    inline void update_uniforms(min::camera<float> &camera, const bool update_bones)
    {
        // Update camera properties
        _uniforms.update_camera(camera);

        // Update particle reference position
        _uniforms.update_particle(_particles.get_reference());

        // Update world preview matrix
        _uniforms.update_preview(_world.get_preview_position());

        // Update md5 model matrix
        _uniforms.update_md5_model(_state.get_model_matrix());

        // Update mob positions
        _uniforms.update_mobs(_world.get_mob_positions());

        // Update md5 model bones
        if (update_bones)
        {
            _uniforms.update_bones(_character.get_bones());
        }

        // Update all matrices
        _uniforms.update_matrix_buffer();
    }

  public:
    // Load window shaders and program
    mglcraft()
        : _win("MGLCRAFT", 720, 480, 3, 3),
          _text(28),
          _uniforms(),
          _particles(_uniforms),
          _character(&_particles, _uniforms),
          _world(load_state(), &_particles, _uniforms, 64, 8, 7),
          _controls(_win, _state.get_camera(), _character, _state, _text, _world),
          _goal_seek(_world), _fps(0.0), _idle(0.0)
    {
        // Set depth and cull settings
        min::settings::initialize();

        // Load text into the text buffer
        load_text();

        // Turn off cursor
        _win.display_cursor(false);

        // Maximize window
        _win.maximize();

        // Update cursor position for tracking
        update_cursor();

        // Test adding a mob
        const min::vec3<float> p(-4.5, 30.5, 4.5);
        _world.add_mob(p);
    }
    ~mglcraft()
    {
        // Save game data to file
        save_state();
    }
    void clear_background() const
    {
        // blue background
        const float color[] = {0.690, 0.875f, 0.901f, 1.0f};
        glClearBufferfv(GL_COLOR, 0, color);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    void draw(const float dt)
    {
        // Get player physics body position
        const min::vec3<float> &p = _world.character_position();

        // get user input
        const auto c = user_input();

        // Must update state properties, camera before drawing world
        _state.update(p, c, _win.get_width(), _win.get_height(), dt);

        // Update the character
        min::camera<float> &camera = _state.get_camera();

        // If AI is in control
        if (_world.get_ai_mode())
        {
            // Perform goal seek
            _goal_seek.seek(_world, 0);
        }

        // Update the world state
        _world.update(camera, dt);

        // Update the particle system
        _particles.update(camera, _world.character_velocity(), dt);

        // Update the character state
        const bool update = _character.update(camera, dt);

        // Update all uniforms
        update_uniforms(camera, update);

        // Draw world geometry
        _world.draw(_uniforms);

        // Draw the character if fire mode activated
        if (_state.get_fire_mode())
        {
            _character.draw(_uniforms);
        }

        // Draw the text
        _text.draw();
    }
    bool is_closed() const
    {
        return _win.get_shutdown();
    }
    bool is_paused() const
    {
        return _state.get_game_pause();
    }
    void set_title(const std::string &title)
    {
        _win.set_title(title);
    }
    void update_cursor()
    {
        // Get the screen dimensions
        const uint16_t h = _win.get_height();
        const uint16_t w = _win.get_width();

        // Center cursor in middle of window
        _win.set_cursor(w / 2, h / 2);
    }
    void update_keyboard(const float dt)
    {
        // Update the keyboard
        auto &keyboard = _win.get_keyboard();
        keyboard.update(dt);
    }
    void update_sync(const double fps, const double idle)
    {
        _fps = fps;
        _idle = idle;
    }
    void update_text()
    {
        // If drawing text mode is on, update text
        if (_text.get_draw())
        {
            // Update player position debug text
            const min::vec3<float> &p = _world.character_position();
            std::ostringstream stream;
            stream << "POS- " << std::fixed << std::setprecision(4) << "X: " << p.x() << ", Y: " << p.y() << ", Z: " << p.z();
            _text.update_text(stream.str(), 2);

            // Clear and reset the stream
            stream.clear();
            stream.str(std::string());

            // Update player direction debug text
            const min::vec3<float> &f = _state.get_camera().get_forward();
            stream << "DIR- X: " << f.x() << ", Y: " << f.y() << ", Z: " << f.z();
            _text.update_text(stream.str(), 3);

            // Clear and reset the stream
            stream.clear();
            stream.str(std::string());

            // Update the game mode text
            const std::string &mode = _state.get_game_mode();
            _text.update_text(mode, 4);

            // Update the destination text
            const min::vec3<float> &goal = _goal_seek.get_goal();
            stream << "DEST- X: " << goal.x() << ", Y: " << goal.y() << ", Z: " << goal.z();
            _text.update_text(stream.str(), 5);

            // Clear and reset the stream
            stream.clear();
            stream.str(std::string());

            // Update the energy text
            stream << "ENERGY: " << _state.get_energy();
            _text.update_text(stream.str(), 6);

            // Clear and reset the stream
            stream.clear();
            stream.str(std::string());

            // Update FPS and IDLE
            stream << "FPS: " << _fps;
            _text.update_text(stream.str(), 7);

            // Clear and reset the stream
            stream.clear();
            stream.str(std::string());

            // Update FPS and IDLE
            stream << "IDLE: " << _idle;
            _text.update_text(stream.str(), 8);

            // Upload changes
            _text.upload();
        }
    }
    void update_window()
    {
        // Update and swap buffers
        _win.update();
        _win.swap_buffers();
    }
};

void run()
{
    // Load window shaders and program, enable shader program
    mglcraft game;

    // Setup controller to run at 60 frames per second
    const size_t frames = 60;
    min::loop_sync sync(frames);
    double frame_time = 0.0;

    // User can close with Q or use window manager
    while (!game.is_closed())
    {
        for (size_t i = 0; i < frames; i++)
        {
            // Start synchronizing the loop
            sync.start();

            // Update the keyboard
            game.update_keyboard(frame_time);

            // Is the game paused?
            const bool skip = game.is_paused();
            if (!skip)
            {
                // Clear the background color
                game.clear_background();

                // Draw the model
                game.draw(frame_time);
            }

            // Update the window after draw command
            game.update_window();

            // Calculate needed delay to hit target
            frame_time = sync.sync();
        }

        // Calculate the number of 'average' frames per second
        const double fps = sync.get_fps();

        // Calculate the percentage of frame spent idle
        const double idle = sync.idle();

        // Update fps and idle text
        game.update_sync(fps, idle);

        // Update the debug text
        game.update_text();
    }
}

int main()
{
    try
    {
        run();
    }
    catch (const std::exception &ex)
    {
        std::cout << ex.what() << std::endl;
    }

    return 0;
}
