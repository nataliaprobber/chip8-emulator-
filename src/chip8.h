#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

class Chip8
{
public:
    std::array<uint8_t, 4096> memory{};
    std::array<uint8_t, 16> V{};
    uint16_t I = 0;
    uint16_t pc = 0x200;

    std::array<uint16_t, 16> stack{};
    uint8_t sp = 0;

    uint8_t delay_timer = 0;
    uint8_t sound_timer = 0;

    std::array<uint8_t, 64 * 32> display{};
    std::array<bool, 16> keys{};

    static constexpr uint8_t fontset[80] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };

    Chip8()
    {
        for (int i = 0; i < 80; i++)
        {
            memory[0x050 + i] = fontset[i];
        }
        // random number generator
        srand(time(nullptr));
    }

    bool load_rom(const char *path)
    {
        // 1. Open the file in binary mode
        FILE *f = fopen(path, "rb");
        if (f == NULL)
        {
            printf("Error: could not open %s\n", path);
            return false;
        }

        // 2. Copy up to 3584 bytes from the file into memory, starting at 0x200
        fread(&memory[0x200], 1, 3584, f);

        // 3. If there's still data left, the game is too big to fit
        if (fgetc(f) != EOF)
        {
            printf("Error: %s is too big (max 3584 bytes)\n", path);
            fclose(f);
            return false;
        }

        // 4. Done: close the file and report success
        fclose(f);
        return true;
    }

    void cycle()
    {
        // FETCH
        uint16_t opcode = (memory[pc] << 8) | memory[pc + 1];
        pc += 2;

        // DECODE: these lines CREATE type, x, y, n, kk, nnn
        uint8_t type = (opcode & 0xF000) >> 12;
        uint8_t x = (opcode & 0x0F00) >> 8;
        uint8_t y = (opcode & 0x00F0) >> 4;
        uint8_t n = opcode & 0x000F;
        uint8_t kk = opcode & 0x00FF;
        uint16_t nnn = opcode & 0x0FFF;

        // EXECUTE: these lines USE them
        switch (type)
        {
        case 0x0:
            if (opcode == 0x00E0)
            { // 00E0: clear the screen
                display.fill(0);
            }
            else if (opcode == 0x00EE)
            { // 00EE: return from function
                sp -= 1;
                pc = stack[sp];
            }
            break;
        case 0x1:
            pc = nnn;
            break;
        case 0xB:
            pc = nnn + V[0];
            break;
        case 0xC:
            V[x] = ((rand() % 256) & kk);
            break;
        case 0x6:
            V[x] = kk;
            break;
        case 0x7:
            V[x] += kk;
            break;

        case 0x8:
            switch (n)
            {
            case 0x0:
                V[x] = V[y];
                break;
            case 0x1:
                V[x] = V[x] | V[y];
                break;
            case 0x2:
                V[x] = V[x] & V[y];
                break;
            case 0x3:
                V[x] = V[x] ^ V[y];
                break;
            case 0x4:
            {
                uint16_t sum = V[x] + V[y];
                V[x] = sum;
                if (sum > 255)
                {
                    V[0xF] = 1;
                }
                else
                {
                    V[0xF] = 0;
                }
                break;
            }
            case 0x5:
            {
                uint8_t no_borrow = (V[x] >= V[y]); // decide the flag BEFORE changing V[x]
                V[x] = V[x] - V[y];                 // wraps automatically if it goes negative
                V[0xF] = no_borrow;                 // flag last
                break;
            }
            case 0x6:
            {
                uint8_t lost_bit = V[x] & 0x1; // save the rightmost bit
                V[x] = V[x] >> 1;              // shift right
                V[0xF] = lost_bit;             // flag = what fell off
                break;
            }
            case 0x7:
            {
                uint8_t no_borrow = (V[y] >= V[x]); // starting amount (VY) big enough?
                V[x] = V[y] - V[x];                 // VY − VX, stored in VX
                V[0xF] = no_borrow;
                break;
            }
            case 0xE:
            {
                uint8_t lost_bit = (V[x] & 0x80) >> 7; // leftmost bit, moved down to 0 or 1
                V[x] = V[x] << 1;                      // shift left
                V[0xF] = lost_bit;
                break;
            }
            }
            break;

        case 0xA:
            I = nnn;
            break;

        case 0xD:
        {
            uint8_t start_x = V[x] % 64; // x position from the register (12)
            uint8_t start_y = V[y] % 32; // y position from the register (8)
            V[0xF] = 0;                  // no collision yet

            for (int row = 0; row < n; row++)
            {                                       // for each of the 15 rows...
                uint8_t row_byte = memory[I + row]; //   get that row's byte from memory

                for (int col = 0; col < 8; col++)
                { //   for each of the 8 pixels in the row...
                    if (row_byte & (0x80 >> col))
                    {                           //     if this pixel is on in the picture...
                        int px = start_x + col; //       its screen x
                        int py = start_y + row; //       its screen y
                        if (px >= 64 || py >= 32)
                            continue; //       skip it if it's off the screen

                        int idx = py * 64 + px; //       which display slot is (px, py)

                        if (display[idx] == 1)
                            V[0xF] = 1;    //       already on? that's a collision
                        display[idx] ^= 1; //       flip the pixel
                    }
                }
            }
            break;
        }
        case 0x3: // 3xkk: skip next instruction if Vx == kk
            if (V[x] == kk)
                pc += 2;
            break;
        case 0x4: // 4xkk: skip next instruction if Vx != kk
            if (V[x] != kk)
                pc += 2;
            break;
        case 0x5: // 5xy0: skip next instruction if Vx == Vy
            if (V[x] == V[y])
                pc += 2;
            break;
        case 0x9: // 9xy0: skip next instruction if Vx != Vy
            if (V[x] != V[y])
                pc += 2;
            break;
        case 0x2:           // 2nnn: call function at nnn
            stack[sp] = pc; // save the return address in the next free slot
            sp++;           // the next free slot is now one higher
            pc = nnn;       // jump to the function
            break;
        case 0xF:
            switch (kk)
            {
            case 0x07:
                V[x] = delay_timer;
                break;
            case 0x29:
                I = 0x050 + V[x] * 5; // I = font sprite for digit V[x]
                break;
            case 0x15:
                delay_timer = V[x];
                break;
            case 0x18:
                sound_timer = V[x];
                break;
            case 0x1E:
                I += V[x];
                break;
            case 0x33: // store hundreds, tens, ones of Vx at I, I+1, I+2
                memory[I] = V[x] / 100;
                memory[I + 1] = (V[x] / 10) % 10;
                memory[I + 2] = V[x] % 10;
                break;
            case 0x55:
                for (int i = 0; i <= x; i++)
                {
                    memory[I + i] = V[i];
                }
                break;
            case 0x65:
                for (int i = 0; i <= x; i++)
                {
                    V[i] = memory[I + i];
                }
                break;
            case 0x0A:
            {
                bool found = false; // haven't found a pressed key yet
                for (int i=0; i<16; i++){
                    if (keys[i] == true){
                        V[x] = i;
                        found = true;
                        break;
                    }
                }if(found == false){
                        pc -= 2; 
                }
                break;
            }
        }
            break;
        case 0xE: // KEY opcodes: EX9E, EXA1
            switch (kk)
            {
            case 0x9E: // EX9E: skip if key V[x] is pressed
                if (keys[V[x]] == true)
                {
                    pc += 2;
                }
                break;
            case 0xA1: // EXA1: skip if key V[x] is not pressed
                if (keys[V[x]] == false)
                {
                    pc += 2;
                }
                break;
            }
            break;
        default:
            printf("Not implemented yet: %04X\n", opcode);
            break;
        }
    }

void print_display()
{
    for (int y = 0; y < 32; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            int slot = y * 64 + x; // 1. find this pixel's slot
            uint8_t pixel = display[slot];

            if (pixel == 1)
                printf("##"); // 2. on → '##', off → '  '
            else
                printf("  ");
        }
        printf("\n");
    }
}
};