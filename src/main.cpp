#define SDL_MAIN_HANDLED // we write our own main(); don't let SDL replace it
#include <SDL.h>
#include <cstdio>
#include <string>
#include <filesystem>
#include <vector>
#include "chip8.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

namespace fs = std::filesystem;

// Maps each CHIP-8 key to a key on a modern keyboard.
// Index = CHIP-8 key (0x0–0xF), value = the keyboard key that triggers it.
// Keeps the original 4x4 keypad shape:   1 2 3 C      1 2 3 4
//                                        4 5 6 D  ->  Q W E R
//                                        7 8 9 E      A S D F
//                                        A 0 B F      Z X C V
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
    SDL_SetMainReady(); // pairs with SDL_MAIN_HANDLED: tells SDL startup is fine

    // ── Emulator + optional ROM from the command line ──
    bool rom_loaded = false; // true once a ROM is in memory and ready to run
    std::string current_rom; // path of the loaded ROM (empty = none); used by Reset

    Chip8 chip;

    if (argc >= 2)
    {
        current_rom = argv[1];
        rom_loaded = chip.load_rom(current_rom.c_str());
    }
    else
    {
        printf("No ROM given. Pick one from the ROMs menu or drag a .ch8 file onto the window.\n");
    }

    // ── SDL: video + audio ──
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Window starts at 10x the CHIP-8 resolution (640x320) and can be resized.
    // SCALE only sets the starting size; drawing recalculates the scale every frame.
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

    // ── Audio device ──
    // Describes the sound we'll send: 44,100 signed 16-bit mono samples per second.
    SDL_AudioSpec want{};
    want.freq = 44100;          // samples per second
    want.format = AUDIO_S16SYS; // each sample is a signed 16-bit number (speaker position)
    want.channels = 1;          // mono: one tone needs one channel
    want.samples = 512;         // SDL's internal buffer size
    want.callback = NULL;       // no callback: we push samples each frame with SDL_QueueAudio

    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio == 0)
    {
        printf("Warning: no audio (%s)\n", SDL_GetError()); // not fatal: run silently
    }
    else
    {
        SDL_PauseAudioDevice(audio, 0); // devices start paused; 0 = start playing
    }

    // ── Dear ImGui (menus + debug window) ──
    IMGUI_CHECKVERSION();                                // headers match the compiled library
    ImGui::CreateContext();                              // ImGui's internal state
    ImGui::StyleColorsDark();                            // theme
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer); // backend: SDL events -> ImGui
    ImGui_ImplSDLRenderer2_Init(renderer);               // backend: ImGui -> SDL drawing

    // ── ROM list for the ROMs menu (scanned once at startup) ──
    // roms/ is git-ignored, so a fresh clone won't have it; create it instead of crashing.
    std::vector<std::string> rom_list;

    if (!fs::exists("roms"))
    {
        fs::create_directory("roms");
        printf("Created a 'roms' folder. Put .ch8 files in it.\n");
    }

    for (const auto &entry : fs::directory_iterator("roms"))
    {
        if (entry.path().extension() == ".ch8")
        {
            rom_list.push_back(entry.path().string());
        }
    }

    // ── Main loop: one iteration = one frame (~60 per second) ──
    bool running = true; // false ends the program (window X, Ctrl+C, or File > Quit)
    bool paused = false; // true freezes the CPU and timers (debug window)
    int wave_pos = 0;    // position in the square wave (0–99); kept across frames so the beep is smooth

    while (running)
    {
        // 1. Events: quit, keyboard, and dropped files
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event); // let ImGui see every event first

            if (event.type == SDL_QUIT)
            {
                running = false;
            }

            // Keyboard: find which CHIP-8 key this real key maps to, and record pressed/released
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

            // A .ch8 file was dragged onto the window: load it
            if (event.type == SDL_DROPFILE)
            {
                current_rom = event.drop.file;               // copy the path (SDL frees the original below)
                chip.reset();                                // wipe the old game first
                rom_loaded = chip.load_rom(event.drop.file); // true if the ROM loaded into memory
                SDL_free(event.drop.file);                   // return SDL's copy of the path
            }
        }

        // 2. CPU + timers: ~600 instructions/sec (10 per frame), timers tick at 60 Hz (once per frame)
        if (rom_loaded && !paused)
        {
            for (int i = 0; i < 10; i++)
            {
                chip.cycle();
            }
            chip.tick_timers();
        }

        // 3. Sound: queue one frame (735 samples = 44100 / 60) of a 440 Hz square wave
        //    while the sound timer is running. One wave = 100 samples: 50 high, 50 low.
        if (audio != 0 && chip.sound_timer > 0)
        {
            int16_t samples[735];
            for (int i = 0; i < 735; i++)
            {
                if (wave_pos < 50)
                {
                    samples[i] = 3000; // high half of the wave
                }
                else
                {
                    samples[i] = -3000; // low half of the wave
                }
                wave_pos = (wave_pos + 1) % 100;
            }
            SDL_QueueAudio(audio, samples, sizeof(samples)); // size in bytes, not samples
        }

        // 4. UI: describe this frame's menus and windows (ImGui rebuilds them every frame)
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Reset")) // restart the current ROM from the beginning
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

            if (ImGui::BeginMenu("ROMs")) // one item per .ch8 file found in roms/
            {
                for (const std::string &path : rom_list)
                {
                    std::string name = fs::path(path).stem().string(); // "roms\pong.ch8" -> "pong"

                    if (ImGui::MenuItem(name.c_str())) // true on the frame it's clicked
                    {
                        current_rom = path;
                        chip.reset();
                        rom_loaded = chip.load_rom(current_rom.c_str());
                    }
                }

                if (rom_list.empty())
                {
                    // disabled (grayed-out) item, just a hint
                    ImGui::MenuItem("(no ROMs found in roms/)", nullptr, false, false);
                }

                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // Debug window: live CPU state, plus pause and single-step
        ImGui::Begin("CPU");

        ImGui::Text("PC: 0x%04X", chip.pc);
        ImGui::Text("I:  0x%04X", chip.I);
        ImGui::Text("SP: %d", chip.sp);
        ImGui::Text("DT: %d", chip.delay_timer);
        ImGui::Text("ST: %d", chip.sound_timer);

        ImGui::Separator();

        for (int i = 0; i < 16; i++)
        {
            ImGui::Text("V%X: %02X", i, chip.V[i]); // register number and value, both in hex
        }

        ImGui::Separator();

        if (ImGui::Button(paused ? "Resume" : "Pause"))
        {
            paused = !paused;
        }

        ImGui::SameLine(); // next button on the same row

        if (ImGui::Button("Step")) // run exactly one instruction while paused
        {
            if (paused && rom_loaded)
            {
                chip.cycle();
            }
        }

        ImGui::End();

        ImGui::Render(); // finish the UI; it's drawn below, on top of the game

        // 5. Draw: clear, draw the CHIP-8 screen scaled to fit, then the UI on top
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Largest whole-number scale that fits 64x32 in the window, centered with black bars
        int win_w, win_h;
        SDL_GetRendererOutputSize(renderer, &win_w, &win_h);
        int scale = win_w / 64;
        if (win_h / 32 < scale)
            scale = win_h / 32;
        int off_x = (win_w - 64 * scale) / 2;
        int off_y = (win_h - 32 * scale) / 2;

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

        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer); // UI over the game

        SDL_RenderPresent(renderer); // show the finished frame

        // 6. Wait ~16 ms so the loop runs at about 60 frames per second
        SDL_Delay(16);
    }

    // ── Cleanup, in reverse order of creation ──
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_CloseAudioDevice(audio);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}