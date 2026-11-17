#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <string>
#include "chip8.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// keymap[chip8_key] = the real keyboard key for it
const SDL_Keycode keymap[16] = {
    SDLK_x,                        // 0
    SDLK_1, SDLK_2, SDLK_3,        // 1, 2, 3
    SDLK_q, SDLK_w, SDLK_e,        // 4, 5, 6
    SDLK_a, SDLK_s, SDLK_d,        // 7, 8, 9
    SDLK_z, SDLK_c,                // A, B
    SDLK_4, SDLK_r, SDLK_f, SDLK_v // C, D, E, F
};

int main(int argc, char *argv[])
{
    SDL_SetMainReady();

    bool rom_loaded = false; // default: nothing loaded yet
    std::string current_rom; // path of the loaded ROM (empty = none)

    Chip8 chip;

    if (argc >= 2)
    {
        current_rom = argv[1];
        rom_loaded = chip.load_rom(current_rom.c_str());
    }
    else
    {
        printf("No ROM given. Drag a .ch8 file onto the window to start.\n");
    }

    // set up SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int SCALE = 10;
    SDL_Window *window = SDL_CreateWindow("CHIP-8", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 64 * SCALE, 32 * SCALE, SDL_WINDOW_RESIZABLE);
    if (window == NULL)
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
    {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Open the audio device (the "window" for your speakers)
    SDL_AudioSpec want{};
    want.freq = 44100;          // 44,100 samples per second
    want.format = AUDIO_S16SYS; // each sample: signed 16-bit number
    want.channels = 1;          // mono: one beep, one channel
    want.samples = 512;
    want.callback = NULL; // we'll push samples in with SDL_QueueAudio

    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio == 0)
    {
        printf("Warning: no audio (%s)\n", SDL_GetError()); // keep running, just silent
    }
    else
    {
        SDL_PauseAudioDevice(audio, 0); // devices start paused; 0 = start playing
    }

    // Set up ImGui
    IMGUI_CHECKVERSION();                                // sanity check: headers match the library
    ImGui::CreateContext();                              // ImGui's internal state (like SDL_Init)
    ImGui::StyleColorsDark();                            // pick a theme
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer); // adapter 1: SDL events → ImGui
    ImGui_ImplSDLRenderer2_Init(renderer);               // adapter 2: ImGui → SDL drawing

    // Find every ROM in the roms folder (runs once, at startup)
    std::vector<std::string> rom_list;
    for (const auto &entry : fs::directory_iterator("roms"))
    {
        if (entry.path().extension() == ".ch8")
        {
            rom_list.push_back(entry.path().string());
        }
    }

    bool running = true;
    bool paused = false;      // starts unpaused, so games run normally
    int wave_pos = 0;

    while (running)
    {
        // 1. Events
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT)
            {
                running = false;
            }
            // Tests CHIP-8 key against the real key pressesd on the keyboard
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
            {
                for (int i = 0; i < 16; i++)
                {
                    if (event.key.keysym.sym == keymap[i])
                    {
                        chip.keys[i] = (event.type == SDL_KEYDOWN);
                    }
                }
            }
            // Checks if file is dropped
            if (event.type == SDL_DROPFILE)
            {
                current_rom = event.drop.file;               // copy the path before freeing it
                chip.reset();                                // wipe the old game
                rom_loaded = chip.load_rom(event.drop.file); // returns true if ROM loads into memory, false otherwise
                SDL_free(event.drop.file);                   // give the path back to SDL
            }
        }

        // 2. Run the CPU + Timers (only if game is loaded)
        if (rom_loaded && !paused)
        {
            for (int i = 0; i < 10; i++)
            {
                chip.cycle();
            }
            chip.tick_timers();
        }

        // 3. Sound: beep while the sound timer is running
        if (audio != 0 && chip.sound_timer > 0)
        {
            int16_t samples[735]; // one frame of sound
            for (int i = 0; i < 735; i++)
            {
                for (int i = 0; i < 735; i++)
                {
                    if (wave_pos < 50)
                    {
                        samples[i] = 3000; // first half: cone pushed out
                    }
                    else
                    {
                        samples[i] = -3000; // second half: cone pulled in
                    }
                    wave_pos = (wave_pos + 1) % 100;
                }
            }
            SDL_QueueAudio(audio, samples, sizeof(samples));
        }

        // 4. ImGui: describe this frame's UI
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Reset"))
                {
                    chip.reset();
                    rom_loaded = chip.load_rom(current_rom.c_str());
                }
                if (ImGui::MenuItem("Quit"))
                {
                    running = false;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Roms"))      // ← NEW: right after File's EndMenu
            {
                for (const std::string &path : rom_list)
                {
                    // make a clean display name: "roms\pong.ch8" → "pong"
                    std::string name = fs::path(path).stem().string();

                    // show it as a menu item; true on the frame it's clicked
                    if (ImGui::MenuItem(name.c_str()))
                    {
                        // load the clicked game (same as drop block)
                        current_rom = path;
                        chip.reset();
                        rom_loaded = chip.load_rom(current_rom.c_str());
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // 5. Debug view: CPU state
        ImGui::Begin("CPU");

        ImGui::Text("PC: 0x%04X", chip.pc);
        ImGui::Text("I:  0x%04X", chip.I);
        ImGui::Text("SP: %d", chip.sp);
        ImGui::Text("DT: %d", chip.delay_timer);
        ImGui::Text("ST: %d", chip.sound_timer);

        ImGui::Separator();          // a horizontal line, to divide the sections

        for (int i = 0; i < 16; i++)
        {
            ImGui::Text("V%X: %02X", i, chip.V[i]);
        }

        ImGui::Separator();                                 

        if (ImGui::Button(paused ? "Resume" : "Pause"))    
        {
            paused = !paused;
        }
        
        ImGui::SameLine();             // put the next button beside this one, not below

        if (ImGui::Button("Step"))     // NEW
        {
            if (paused && rom_loaded)
            {
                chip.cycle();              // run one instruction
            }
        }

        ImGui::End();

        ImGui::Render();

        // Draw chip.display
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Fit the 64x32 game into the current window size
        int win_w, win_h;
        SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
        int scale = win_w / 64;
        if (win_h / 32 < scale)
            scale = win_h / 32;               // pick the smaller fit
        int off_x = (win_w - 64 * scale) / 2; // center left-right
        int off_y = (win_h - 32 * scale) / 2; // center top-bottom

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        for (int y = 0; y < 32; y++)
        {
            for (int x = 0; x < 64; x++)
            {
                if (chip.display[y * 64 + x] == 1)
                {
                    SDL_Rect px = {off_x + x * scale, off_y + y * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &px);
                }
            }
        }

        // Draw ImGui ON TOP of the game
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);

        // Wait for the next frame
        SDL_Delay(16);
    }

    // Clean up, in reverse order of creation
    ImGui_ImplSDLRenderer2_Shutdown(); // undo adapter 2
    ImGui_ImplSDL2_Shutdown();         // undo adapter 1
    ImGui::DestroyContext();           // undo CreateContext

    SDL_CloseAudioDevice(audio);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
