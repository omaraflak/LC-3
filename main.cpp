#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/termios.h>

#define MEMORY_MAX (1 << 16)

enum {
    OP_BR,       // 0000
    OP_ADD,      // 0001
    OP_LD,       // 0010
    OP_ST,       // 0011
    OP_JSR,      // 0100
    OP_AND,      // 0101
    OP_LDR,      // 0110
    OP_STR,      // 0111
    OP_RTI,      // 1000
    OP_NOT,      // 1001
    OP_LDI,      // 1010
    OP_STI,      // 1011
    OP_JMP,      // 1100
    OP_RESERVED, // 1101
    OP_LEA,      // 1110
    OP_TRAP,     // 1111
};

enum {
    R_R0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT
};

enum {
    COND_NEGATIVE = 1 << 2,
    COND_ZERO = 1 << 1,
    COND_POSITIVE = 1 << 0
};

enum {
    TRAP_GETC = 0x20,
    TRAP_OUT = 0x21,
    TRAP_PUTS = 0x22,
    TRAP_IN = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT = 0x25
};

enum {
    MR_KBSR = 0xFE00, // keyboard status
    MR_KBDR = 0xFE02  // keyboard data
};

uint16_t PC_START = 0x3000;
uint16_t memory[MEMORY_MAX];
uint16_t registers[R_COUNT];

struct termios original_tio;

void disable_input_buffering() {
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

uint16_t bits(uint16_t x, uint8_t start, uint8_t stop) {
    uint8_t count = stop - start + 1;
    uint16_t mask = ((1 << count) - 1) << start;
    return (x & mask) >> start;
}

uint16_t bit(uint16_t x, uint8_t index) {
    return bits(x, index, index);
}

uint16_t sign_extend(uint16_t value, uint16_t bits) {
    uint16_t mask = 1 << (bits - 1);
    if ((value & mask) == 0) {
        return value;
    }
    uint16_t sign = ((1 << (16 - bits)) - 1) << bits;
    return sign + value;
}

void set_memory(uint16_t address, uint16_t value) {
    memory[address] = value;
}

uint16_t get_memory(uint16_t address) {
    if (address == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        } else {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

void set_condition_codes(uint16_t value) {
    if (value == 0) {
        registers[R_COND] = COND_ZERO;
    } else if (bit(value, 15) == 1) {
        registers[R_COND] = COND_NEGATIVE;
    } else {
        registers[R_COND] = COND_POSITIVE;
    }
}

uint16_t swap16(uint16_t x) {
    return (x << 8) | (x >> 8);
}

bool load_program(std::string filename) {
    std::ifstream is(filename.c_str(), std::ios::binary);
    if (!is.is_open()) {
        std::cout << "Could not open file: " << filename << std::endl;
        return false;
    }

    uint16_t origin;
    is.read((char*) &origin, sizeof(uint16_t));
    origin = swap16(origin);
    registers[R_PC] = origin;

    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t *p = memory + origin;
    is.read((char*) p, (sizeof(uint16_t) / sizeof(char)) * max_read);
    is.close();

    while (p < memory + MEMORY_MAX) {
        *p = swap16(*p);
        p++;
    }
    return true;
}

void execute() {
    bool running = true;
    while(running) {
        uint16_t instruction = get_memory(registers[R_PC]++);
        uint16_t opcode = bits(instruction, 12, 15);

        switch (opcode) {
            case OP_BR: {
                bool negative = bit(instruction, 11);
                bool zero = bit(instruction, 10);
                bool positive = bit(instruction, 9);
                uint16_t pc_offset = bits(instruction, 0, 8);
                uint16_t conditions = registers[R_COND];
                bool reg_negative = bit(conditions, 2);
                bool reg_zero = bit(conditions, 1);
                bool reg_positive = bit(conditions, 0);
                if ((negative && reg_negative) || (zero && reg_zero) || (positive && reg_positive)) {
                    registers[R_PC] += sign_extend(pc_offset, 9);
                }
                break;
            }
            case OP_ADD: {
                uint16_t destination = bits(instruction, 9, 11);
                uint16_t source1 = bits(instruction, 6, 8);
                uint16_t immediate = bit(instruction, 5);
                uint16_t result;
                if (immediate == 0) {
                    uint16_t source2 = bits(instruction, 0, 2);
                    result = registers[source1] + registers[source2];
                } else {
                    uint16_t value = bits(instruction, 0, 4);
                    result = registers[source1] + sign_extend(value, 5);
                }
                registers[destination] = result;
                set_condition_codes(result);
                break;
            }
            case OP_LD: {
                uint16_t destination = bits(instruction, 9, 11);
                uint16_t offset = bits(instruction, 0, 8);
                uint16_t address = registers[R_PC] + sign_extend(offset, 9);
                uint16_t value = get_memory(address);
                registers[destination] = value;
                set_condition_codes(value);
                break;
            }
            case OP_ST: {
                uint16_t source = bits(instruction, 9, 11);
                uint16_t offset = bits(instruction, 0, 8);
                uint16_t address = registers[R_PC] + sign_extend(offset, 9);
                set_memory(address, registers[source]);
                break;
            }
            case OP_JSR: {
                registers[R_R7] = registers[R_PC];
                if (bit(instruction, 11) == 0) {
                    uint16_t source = bits(instruction, 6, 8);
                    registers[R_PC] = registers[source];
                } else {
                    uint16_t address = bits(instruction, 0, 10);
                    registers[R_PC] += sign_extend(address, 11);
                }
                break;
            }
            case OP_AND: {
                uint16_t destination = bits(instruction, 9, 11);
                uint16_t source1 = bits(instruction, 6, 8);
                uint16_t immediate = bit(instruction, 5);
                uint16_t result;
                if (immediate == 0) {
                    uint16_t source2 = bits(instruction, 0, 2);
                    result = registers[source1] & registers[source2];
                } else {
                    uint16_t value = bits(instruction, 0, 4);
                    result = registers[source1] & sign_extend(value, 5);
                }
                registers[destination] = result;
                set_condition_codes(result);
                break;
            }
            case OP_LDR: {
                uint16_t destination = bits(instruction, 9, 11);
                uint16_t base = bits(instruction, 6, 8);
                uint16_t offset = bits(instruction, 0, 5);
                uint16_t address = registers[base] + sign_extend(offset, 6);
                uint16_t value = get_memory(address);
                registers[destination] = value;
                set_condition_codes(value);
                break;
            }
            case OP_STR: {
                uint16_t source = bits(instruction, 9, 11);
                uint16_t base = bits(instruction, 6, 8);
                uint16_t offset = bits(instruction, 0, 5);
                uint16_t address = registers[base] + sign_extend(offset, 6);
                set_memory(address, registers[source]);
                break;
            }
            case OP_RTI: {
                std::cout << "UNIMPLEMENTED OP CODE: " << opcode << std::endl;
                running = false;
                break;
            }
            case OP_NOT: {
                uint16_t destination = bits(instruction, 9, 11);
                uint16_t source = bits(instruction, 6, 8);
                uint16_t result = ~registers[source];
                registers[destination] = result;
                set_condition_codes(result);
                break;
            }
            case OP_LDI: {
                uint16_t destination = bits(instruction, 9, 11);
                uint16_t offset = bits(instruction, 0, 8);
                uint16_t address = registers[R_PC] + sign_extend(offset, 9);
                uint16_t value = get_memory(get_memory(address));
                registers[destination] = value;
                set_condition_codes(value);
                break;
            }
            case OP_STI: {
                uint16_t source = bits(instruction, 9, 11);
                uint16_t offset = bits(instruction, 0, 8);
                uint16_t address = registers[R_PC] + sign_extend(offset, 9);
                set_memory(get_memory(address), registers[source]);
                break;
            }
            case OP_JMP: {
                uint16_t source = bits(instruction, 6, 8);
                registers[R_PC] = registers[source];
                break;
            }
            case OP_RESERVED: {
                std::cout << "UNIMPLEMENTED OP CODE: " << opcode << std::endl;
                running = false;
                break;
            }
            case OP_LEA: {
                uint16_t destination = bits(instruction, 9, 11);
                uint16_t offset = bits(instruction, 0, 8);
                uint16_t value = registers[R_PC] + sign_extend(offset, 9);
                registers[destination] = value;
                set_condition_codes(value);
                break;
            }
            case OP_TRAP: {
                registers[R_R7] = registers[R_PC];
                uint16_t trap_vector = bits(instruction, 0, 7);
                switch(trap_vector) {
                    case TRAP_GETC: {
                        registers[R_R0] = (uint16_t) getchar();
                        set_condition_codes(registers[R_R0]);
                        break;
                    }
                    case TRAP_OUT: {
                        char c = bits(registers[R_R0], 0, 7);
                        std::cout << c << std::flush;
                        break;
                    }
                    case TRAP_PUTS: {
                        uint16_t address = registers[R_R0];
                        char char_read = 1;
                        while ((char_read = get_memory(address)) != 0) {
                            std::cout << char_read;
                            address++;
                        }
                        std::cout << std::flush;
                        break;
                    }
                    case TRAP_IN: {
                        std::cout << "Press a key: ";
                        char c = getchar();
                        std::cout << c << std::flush;
                        registers[R_R0] = (uint16_t) c;
                        set_condition_codes(registers[R_R0]);
                        break;
                    }
                    case TRAP_PUTSP: {
                        uint16_t address = registers[R_R0];
                        uint16_t mem_read = 1;
                        while ((mem_read = get_memory(address)) != 0) {
                            char char1 = bits(mem_read, 0, 7);
                            char char2 = bits(mem_read, 8, 15);
                            std::cout << char1;
                            if (char2 != 0) {
                                std::cout << char2;
                            }
                            address++;
                        }
                        std::cout << std::flush;
                        break;
                    }
                    case TRAP_HALT: {
                        std::cout << "HALT" << std::flush;
                        running = false;
                        break;
                    }
                }
                break;
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Syntax is: " << argv[0] << " [file.obj]" << std::endl;
        exit(1);
    }
    if (!load_program(argv[1])) {
        exit(1);
    }
    disable_input_buffering();
    execute();
    restore_input_buffering();
}