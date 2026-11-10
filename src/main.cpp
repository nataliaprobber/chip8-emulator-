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

int main()
{
    Chip8 chip;

    if (!chip.load_rom("4-flags.ch8"))
    {
        return 1; // loading failed, so stop
    }

    // set up SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int SCALE = 10;
    SDL_Window *window = SDL_CreateWindow("CHIP-8", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 64 * SCALE, 32 * SCALE, 0);
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

    bool running = true;
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
        }

        // 2. Run the CPU
        for (int i = 0; i < 50; i++)
        { // run 50 instructions to draw the logo
            chip.cycle();
        }

        // Tick the timers once
        chip.tick_timers();

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
                    SDL_Rect px = {x * SCALE, y * SCALE, SCALE, SCALE};
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
