#include "simulator.h"
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
std::string hex32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}
}

uint32_t Simulator::sign_extend(uint32_t value, unsigned bits) {
    // Trabajar con unsigned evita overflow con signo en C++.
    uint32_t sign = uint32_t(1) << (bits - 1);
    return (value ^ sign) - sign;
}

bool Simulator::signed_less(uint32_t a, uint32_t b) {
    // Cambiar el bit de signo permite comparar enteros con signo usando unsigned.
    return (a ^ 0x80000000u) < (b ^ 0x80000000u);
}

uint32_t Simulator::arithmetic_right(uint32_t value, unsigned amount) {
    amount &= 31;
    if (amount == 0) return value;
    uint32_t result = value >> amount;
    if (value & 0x80000000u) result |= 0xFFFFFFFFu << (32 - amount);
    return result;
}

void Simulator::fail(const std::string& reason) {
    halted = true;
    message = "Error en PC=" + hex32(pc) + ": " + reason;
}

uint32_t Simulator::get_reg(unsigned index) const {
    if (index >= 32) throw std::out_of_range("El registro debe ser x0..x31.");
    return regs[index];
}

uint8_t Simulator::read8(uint32_t address) const {
    auto it = memory.find(address);
    return it == memory.end() ? 0 : it->second;
}

uint16_t Simulator::read16(uint32_t address) const {
    return uint16_t(uint32_t(read8(address)) |
                    (uint32_t(read8(address + 1u)) << 8));
}

uint32_t Simulator::read32(uint32_t address) const {
    return uint32_t(read8(address)) |
           (uint32_t(read8(address + 1u)) << 8) |
           (uint32_t(read8(address + 2u)) << 16) |
           (uint32_t(read8(address + 3u)) << 24);
}

void Simulator::write8(uint32_t address, uint8_t value) {
    if (value == 0) memory.erase(address);
    else memory[address] = value;
}

void Simulator::write16(uint32_t address, uint16_t value) {
    for (unsigned i = 0; i < 2; ++i) write8(address + i, uint8_t(value >> (8 * i)));
}

void Simulator::write32(uint32_t address, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) write8(address + i, uint8_t(value >> (8 * i)));
}

bool Simulator::load_from_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) { message = "No se pudo abrir el archivo."; return false; }
    std::streamoff size = file.tellg();
    if (size <= 0 || uint64_t(size) > (uint64_t(1) << 32) ||
        uint64_t(size) > std::numeric_limits<size_t>::max()) {
        message = "El binario debe contener entre 1 byte y 4 GiB.";
        return false;
    }
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size), 0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        message = "No se pudo leer el archivo completo."; return false;
    }
    if (bytes.size() >= 4 && bytes[0] == 0x7F && bytes[1] == 'E' &&
        bytes[2] == 'L' && bytes[3] == 'F') {
        message = "El archivo es ELF. Exporta un binario crudo (Raw).";
        return false;
    }
    program.swap(bytes);
    reset();
    return true;
}

void Simulator::reset() {
    pc = 0;
    for (auto& reg : regs) reg = 0;
    memory.clear();
    for (size_t i = 0; i < program.size(); ++i) write8(uint32_t(i), program[i]);
    halted = false;
    message.clear();
    steps = 0;
}

bool Simulator::step() {
    if (halted) return false;
    if (pc & 3u) { fail("direccion de instruccion no alineada a 4 bytes."); return false; }

    uint32_t instr = read32(pc);
    uint32_t opcode = instr & 0x7F;
    uint32_t rd = (instr >> 7) & 31;
    uint32_t funct3 = (instr >> 12) & 7;
    uint32_t rs1 = (instr >> 15) & 31;
    uint32_t rs2 = (instr >> 20) & 31;
    uint32_t funct7 = instr >> 25;
    uint32_t a = regs[rs1], b = regs[rs2];
    uint32_t imm_i = sign_extend(instr >> 20, 12);
    uint32_t imm_s = sign_extend(((instr >> 25) << 5) | ((instr >> 7) & 31), 12);
    uint32_t imm_b = sign_extend(((instr >> 31) << 12) |
        (((instr >> 7) & 1) << 11) | (((instr >> 25) & 63) << 5) |
        (((instr >> 8) & 15) << 1), 13);
    uint32_t imm_u = instr & 0xFFFFF000u;
    uint32_t imm_j = sign_extend(((instr >> 31) << 20) |
        (((instr >> 12) & 255) << 12) | (((instr >> 20) & 1) << 11) |
        (((instr >> 21) & 1023) << 1), 21);

    uint32_t next_pc = pc + 4u;
    uint32_t result = 0;
    bool writes_rd = false;
    bool valid = true;

    switch (opcode) {
        case 0x03: { // lb, lh, lw, lbu, lhu
            uint32_t address = a + imm_i;
            writes_rd = true;
            switch (funct3) {
                case 0: result = sign_extend(read8(address), 8); break;
                case 1: result = sign_extend(read16(address), 16); break;
                case 2: result = read32(address); break;
                case 4: result = read8(address); break;
                case 5: result = read16(address); break;
                default: valid = false;
            }
            break;
        }
        case 0x13: 
            writes_rd = true;
            switch (funct3) {
                case 0: result = a + imm_i; break;                         // addi
                case 1:                                                   // slli
                    valid = funct7 == 0; result = a << rs2; break;
                case 2: result = signed_less(a, imm_i) ? 1 : 0; break;     // slti
                case 3: result = a < imm_i ? 1 : 0; break;                 // sltiu
                case 4: result = a ^ imm_i; break;                         // xori
                case 5:
                    if (funct7 == 0) result = a >> rs2;                    // srli
                    else if (funct7 == 0x20) result = arithmetic_right(a, rs2); // srai
                    else valid = false;
                    break;
                case 6: result = a | imm_i; break;                         // ori
                case 7: result = a & imm_i; break;                         // andi
            }
            break;
        case 0x17: writes_rd = true; result = pc + imm_u; break; // auipc
        case 0x23: { // sb, sh, sw
            uint32_t address = a + imm_s;
            switch (funct3) {
                case 0: write8(address, uint8_t(b)); break;
                case 1: write16(address, uint16_t(b)); break;
                case 2: write32(address, b); break;
                default: valid = false;
            }
            break;
        }
        case 0x33: 
            writes_rd = true;
            if (funct7 != 0 && !(funct7 == 0x20 && (funct3 == 0 || funct3 == 5))) {
                valid = false; break;
            }
            switch (funct3) {
                case 0: result = funct7 == 0x20 ? a - b : a + b; break; // sub, add
                case 1: result = a << (b & 31); break;                  // sll
                case 2: result = signed_less(a, b) ? 1 : 0; break;     // slt
                case 3: result = a < b ? 1 : 0; break;                 // sltu
                case 4: result = a ^ b; break;                         // xor
                case 5: result = funct7 == 0x20 ? arithmetic_right(a, b) : a >> (b & 31); break;
                case 6: result = a | b; break;                         // or
                case 7: result = a & b; break;                         // and
            }
            break;
        case 0x37: writes_rd = true; result = imm_u; break; // lui
        case 0x63: { 
            bool take = false;
            switch (funct3) {
                case 0: take = a == b; break;                    // beq
                case 1: take = a != b; break;                    // bne
                case 4: take = signed_less(a, b); break;         // blt
                case 5: take = !signed_less(a, b); break;        // bge
                case 6: take = a < b; break;                     // bltu
                case 7: take = a >= b; break;                    // bgeu
                default: valid = false;
            }
            if (take) next_pc = pc + imm_b;
            break;
        }
        case 0x67: // jalr
            valid = funct3 == 0;
            writes_rd = true;
            result = pc + 4u;
            next_pc = (a + imm_i) & ~uint32_t(1);
            break;
        case 0x6F: // jal
            writes_rd = true;
            result = pc + 4u;
            next_pc = pc + imm_j;
            break;
        case 0x73:
            if (instr == 0x00100073u) {
                halted = true; message = "Programa detenido por EBREAK.";
            } else if (instr == 0x00000073u && (regs[17] == 10 || regs[17] == 93)) {
                halted = true; message = "Programa finalizado por ECALL.";
            } else {
                fail("ECALL no soportada o instruccion SYSTEM invalida."); return false;
            }
            ++steps;
            return true;
        default: valid = false;
    }

    if (!valid) { fail("instruccion no soportada: " + hex32(instr)); return false; }
    if (next_pc & 3u) {
        fail("destino de salto no alineado: " + hex32(next_pc)); return false;
    }
    if (writes_rd && rd != 0) regs[rd] = result;
    regs[0] = 0; 
    pc = next_pc;
    ++steps;
    message.clear();
    return true;
}

uint64_t Simulator::run(uint64_t limit) {
    uint64_t executed = 0;
    while (executed < limit && !halted) {
        if (!step()) break;
        ++executed;
    }
    if (!halted && executed == limit) message = "Limite alcanzado; puedes continuar con step o run.";
    return executed;
}
