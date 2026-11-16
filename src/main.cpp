#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include "chip8.h"

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

    Chip8 chip;

    if (argc >= 2){
        rom_loaded = chip.load_rom(argv[1]);
    }
    else{
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

    SDL_RenderSetLogicalSize(renderer, 64, 32);

    // Open the audio device (the "window" for your speakers)
    SDL_AudioSpec want{};
    want.freq     = 44100;          // 44,100 samples per second
    want.format   = AUDIO_S16SYS;   // each sample: signed 16-bit number
    want.channels = 1;              // mono: one beep, one channel
    want.samples  = 512;
    want.callback = NULL;           // we'll push samples in with SDL_QueueAudio

    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio == 0)
    {
        printf("Warning: no audio (%s)\n", SDL_GetError());   // keep running, just silent
    }
    else
    {
        SDL_PauseAudioDevice(audio, 0);   // devices start paused; 0 = start playing
    }

    bool running = true;
    int wave_pos = 0; 

    while (running)
    {
        // 1. Events
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
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
                chip.reset();                   // wipe the old game
                rom_loaded = chip.load_rom(event.drop.file); // returns true if ROM loads into memory, false otherwise
                SDL_free(event.drop.file);      // give the path back to SDL
            }
        }

        // 2. Run the CPU + Timers (only if game is loaded)
        if (rom_loaded)              
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
            int16_t samples[735];                 // one frame of sound
            for (int i = 0; i < 735; i++)
            {
            for (int i = 0; i < 735; i++)
            {
                if (wave_pos < 50)
                {
                    samples[i] = 3000;     // first half: cone pushed out
                }
                else
                {
                    samples[i] = -3000;    // second half: cone pulled in
                }
                wave_pos = (wave_pos + 1) % 100;
            }
            }
            SDL_QueueAudio(audio, samples, sizeof(samples));
        }

        // 4. Draw chip.display
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        for (int y = 0; y < 32; y++)
        {
            for (int x = 0; x < 64; x++)
            {
                if (chip.display[y * 64 + x] == 1)
                {
                    SDL_Rect px = {x, y, 1, 1};
                    SDL_RenderFillRect(renderer, &px);
                }
            }
        }

        SDL_RenderPresent(renderer);

        // 5. Wait for the next frame
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
