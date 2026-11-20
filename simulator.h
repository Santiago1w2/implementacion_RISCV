#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// El nucleo no depende del menu: una futura interfaz puede usar esta clase.
class Simulator {
private:
    uint32_t pc = 0;
    uint32_t regs[32] = {};
    // Memoria por bytes. Las direcciones no escritas contienen cero.
    // Asi podemos usar cualquier direccion de 32 bits sin reservar 4 GiB.
    std::map<uint32_t, uint8_t> memory;
    std::vector<uint8_t> program;
    bool halted = false;
    std::string message;
    uint64_t steps = 0;

    static uint32_t sign_extend(uint32_t value, unsigned bits);
    static bool signed_less(uint32_t a, uint32_t b);
    static uint32_t arithmetic_right(uint32_t value, unsigned amount);
    void fail(const std::string& reason);

public:
    bool load_from_file(const std::string& filename);
    void reset();
    bool step();
    uint64_t run(uint64_t limit = 1000000);

    uint32_t get_pc() const { return pc; }
    uint32_t get_reg(unsigned index) const;
    bool is_halted() const { return halted; }
    const std::string& get_message() const { return message; }
    uint64_t get_steps() const { return steps; }
    uint8_t read8(uint32_t address) const;
    uint16_t read16(uint32_t address) const;
    uint32_t read32(uint32_t address) const;
    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);
};

#endif
