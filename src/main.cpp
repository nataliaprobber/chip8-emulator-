#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include "chip8.h"

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
        }

        // 2. Run the CPU
        for (int i = 0; i < 50; i++)
        { // run 50 instructions to draw the logo
            chip.cycle();
        }

        chip.print_display(); // shows the screen

        // 3. Draw chip.display
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

        // 4. Wait for the next frame
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}