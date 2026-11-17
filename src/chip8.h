#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

// The CHIP-8 virtual machine: memory, registers, timers, display, and the CPU.
// Knows nothing about SDL: main.cpp feeds it keys and draws its display.
class Chip8
{
public:
    // ── Machine state ──
    std::array<uint8_t, 4096> memory{}; // 4 KB RAM: font at 0x050, programs start at 0x200
    std::array<uint8_t, 16> V{};        // registers V0–VF (VF doubles as the flag register)
    uint16_t I = 0;                     // index register: holds memory addresses
    uint16_t pc = 0x200;                // program counter: address of the next instruction

    std::array<uint16_t, 16> stack{}; // return addresses for subroutine calls
    uint8_t sp = 0;                   // stack pointer: next free slot in the stack

    uint8_t delay_timer = 0; // counts down at 60 Hz; games use it for timing
    uint8_t sound_timer = 0; // counts down at 60 Hz; beeps while above 0

    std::array<uint8_t, 64 * 32> display{}; // 64x32 pixels, 1 = on; index = y * 64 + x
    std::array<bool, 16> keys{};            // hex keypad 0–F, true = held down

    // Built-in sprites for hex digits 0–F: 5 bytes each (one byte per row,
    // left 4 bits used). Loaded at 0x050 so FX29 can point I at them.
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
        reset();
        srand(time(nullptr)); // seed rand() once, for CXKK
    }

    // Power-cycle the machine: clear all state and reload the font.
    // Call before loading a new ROM.
    void reset()
    {
        memory.fill(0);
        V.fill(0);
        stack.fill(0);
        display.fill(0);
        keys.fill(false);

        I = 0;
        pc = 0x200; // programs start at 0x200
        sp = 0;
        delay_timer = 0;
        sound_timer = 0;

        // Clearing memory erased the font, so copy it back in at 0x050
        for (int i = 0; i < 80; i++)
        {
            memory[0x050 + i] = fontset[i];
        }
    }

    // Copy a ROM file into memory at 0x200.
    // Returns true if it loaded, false if the file couldn't be opened or is too big.
    bool load_rom(const char *path)
    {
        FILE *f = fopen(path, "rb"); // "rb" = read, binary mode
        if (f == NULL)
        {
            printf("Error: could not open %s\n", path);
            return false;
        }

        // Programs can use 0x200–0xFFF: 4096 - 512 = 3584 bytes
        fread(&memory[0x200], 1, 3584, f);

        // Any bytes left over means the ROM doesn't fit
        if (fgetc(f) != EOF)
        {
            printf("Error: %s is too big (max 3584 bytes)\n", path);
            fclose(f);
            return false;
        }

        fclose(f);
        return true;
    }

    // Run one instruction: fetch, decode, execute.
    void cycle()
    {
        // FETCH: instructions are 2 bytes, big-endian (high byte first)
        uint16_t opcode = (memory[pc] << 8) | memory[pc + 1];
        pc += 2;

        // DECODE: split the opcode into its fields
        //   opcode = T X Y N   (one hex digit each)
        uint8_t type = (opcode & 0xF000) >> 12; // first digit: which instruction group
        uint8_t x = (opcode & 0x0F00) >> 8;     // second digit: a register number
        uint8_t y = (opcode & 0x00F0) >> 4;     // third digit: another register number
        uint8_t n = opcode & 0x000F;            // last digit: a 4-bit number
        uint8_t kk = opcode & 0x00FF;           // last two digits: an 8-bit number
        uint16_t nnn = opcode & 0x0FFF;         // last three digits: a 12-bit address

        // EXECUTE
        switch (type)
        {
        case 0x0:
            if (opcode == 0x00E0) // 00E0: clear the screen
            {
                display.fill(0);
            }
            else if (opcode == 0x00EE) // 00EE: return from subroutine
            {
                sp -= 1;
                pc = stack[sp];
            }
            break;

        case 0x1: // 1NNN: jump to NNN
            pc = nnn;
            break;

        case 0x2:           // 2NNN: call subroutine at NNN
            stack[sp] = pc; // save the return address (pc already points past this instruction)
            sp++;
            pc = nnn;
            break;

        case 0x3: // 3XKK: skip next instruction if VX == KK
            if (V[x] == kk)
                pc += 2;
            break;

        case 0x4: // 4XKK: skip next instruction if VX != KK
            if (V[x] != kk)
                pc += 2;
            break;

        case 0x5: // 5XY0: skip next instruction if VX == VY
            if (V[x] == V[y])
                pc += 2;
            break;

        case 0x6: // 6XKK: VX = KK
            V[x] = kk;
            break;

        case 0x7: // 7XKK: VX += KK (no carry flag)
            V[x] += kk;
            break;

        case 0x8: // 8XYN: register math/logic; the last digit picks the operation
            switch (n)
            {
            case 0x0: // 8XY0: VX = VY
                V[x] = V[y];
                break;
            case 0x1: // 8XY1: VX = VX OR VY
                V[x] = V[x] | V[y];
                break;
            case 0x2: // 8XY2: VX = VX AND VY
                V[x] = V[x] & V[y];
                break;
            case 0x3: // 8XY3: VX = VX XOR VY
                V[x] = V[x] ^ V[y];
                break;
            case 0x4: // 8XY4: VX = VX + VY, VF = 1 on carry
            {
                uint16_t sum = V[x] + V[y]; // 16-bit so a carry isn't lost before we check it
                V[x] = sum;                 // keeps the low 8 bits
                if (sum > 255)
                {
                    V[0xF] = 1;
                }
                else
                {
                    V[0xF] = 0;
                }
                break; // VF is written last so it wins if X is F
            }
            case 0x5: // 8XY5: VX = VX - VY, VF = 1 if no borrow
            {
                uint8_t no_borrow = (V[x] >= V[y]); // decide before V[x] changes
                V[x] = V[x] - V[y];                 // wraps around if negative
                V[0xF] = no_borrow;                 // flag last
                break;
            }
            case 0x6: // 8XY6: VX >>= 1, VF = the bit shifted out
            {
                uint8_t lost_bit = V[x] & 0x1;
                V[x] = V[x] >> 1;
                V[0xF] = lost_bit;
                break;
            }
            case 0x7: // 8XY7: VX = VY - VX, VF = 1 if no borrow
            {
                uint8_t no_borrow = (V[y] >= V[x]);
                V[x] = V[y] - V[x];
                V[0xF] = no_borrow;
                break;
            }
            case 0xE: // 8XYE: VX <<= 1, VF = the bit shifted out
            {
                uint8_t lost_bit = (V[x] & 0x80) >> 7;
                V[x] = V[x] << 1;
                V[0xF] = lost_bit;
                break;
            }
            }
            break;

        case 0x9: // 9XY0: skip next instruction if VX != VY
            if (V[x] != V[y])
                pc += 2;
            break;

        case 0xA: // ANNN: I = NNN
            I = nnn;
            break;

        case 0xB: // BNNN: jump to NNN + V0
            pc = nnn + V[0];
            break;

        case 0xC: // CXKK: VX = random byte AND KK
            V[x] = ((rand() % 256) & kk);
            break;

        case 0xD: // DXYN: draw an N-row sprite from memory[I] at (VX, VY); VF = 1 on collision
        {
            uint8_t start_x = V[x] % 64; // starting position wraps to the screen
            uint8_t start_y = V[y] % 32;
            V[0xF] = 0;

            for (int row = 0; row < n; row++) // each row of the sprite is one byte
            {
                uint8_t row_byte = memory[I + row];

                for (int col = 0; col < 8; col++) // each bit is one pixel, left to right
                {
                    if (row_byte & (0x80 >> col)) // is this pixel set in the sprite?
                    {
                        int px = start_x + col;
                        int py = start_y + row;
                        if (px >= 64 || py >= 32)
                            continue; // clip pixels past the screen edge

                        int idx = py * 64 + px;

                        if (display[idx] == 1)
                            V[0xF] = 1;    // turning off a lit pixel = collision
                        display[idx] ^= 1; // sprites are XOR-drawn
                    }
                }
            }
            break;
        }

        case 0xE: // EX__: keypad checks; the last two digits pick which
            switch (kk)
            {
            case 0x9E: // EX9E: skip next instruction if key VX is pressed
                if (keys[V[x]] == true)
                {
                    pc += 2;
                }
                break;
            case 0xA1: // EXA1: skip next instruction if key VX is not pressed
                if (keys[V[x]] == false)
                {
                    pc += 2;
                }
                break;
            }
            break;

        case 0xF: // FX__: timers, keys, font, BCD, memory; the last two digits pick which
            switch (kk)
            {
            case 0x07: // FX07: VX = delay timer
                V[x] = delay_timer;
                break;
            case 0x0A: // FX0A: wait for a key press, store the key in VX
            {
                bool found = false;
                for (int i = 0; i < 16; i++)
                {
                    if (keys[i] == true)
                    {
                        V[x] = i;
                        found = true;
                        break;
                    }
                }
                if (found == false)
                {
                    pc -= 2; // no key yet: rerun this instruction next cycle
                }
                break;
            }
            case 0x15: // FX15: delay timer = VX
                delay_timer = V[x];
                break;
            case 0x18: // FX18: sound timer = VX
                sound_timer = V[x];
                break;
            case 0x1E: // FX1E: I += VX
                I += V[x];
                break;
            case 0x29: // FX29: I = address of the font sprite for digit VX
                I = 0x050 + V[x] * 5;
                break;
            case 0x33: // FX33: store VX's hundreds, tens, ones digits at I, I+1, I+2
                memory[I] = V[x] / 100;
                memory[I + 1] = (V[x] / 10) % 10;
                memory[I + 2] = V[x] % 10;
                break;
            case 0x55: // FX55: store V0 through VX into memory starting at I
                for (int i = 0; i <= x; i++)
                {
                    memory[I + i] = V[i];
                }
                break;
            case 0x65: // FX65: load V0 through VX from memory starting at I
                for (int i = 0; i <= x; i++)
                {
                    V[i] = memory[I + i];
                }
                break;
            }
            break;

        default:
            printf("Unknown opcode: %04X\n", opcode);
            break;
        }
    }

    // Count both timers down by 1, stopping at 0. Called 60 times per second.
    void tick_timers()
    {
        if (delay_timer > 0)
        {
            delay_timer--;
        }
        if (sound_timer > 0)
        {
            sound_timer--;
        }
    }

    // Debug helper: print the display to the terminal as text ("##" = on).
    // Not used by the emulator itself.
    void print_display()
    {
        for (int y = 0; y < 32; y++)
        {
            for (int x = 0; x < 64; x++)
            {
                if (display[y * 64 + x] == 1)
                    printf("##");
                else
                    printf("  ");
            }
            printf("\n");
        }
    }
};