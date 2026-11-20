#include "simulator.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

const char* names[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

uint32_t number(const std::string& text) {
    if (text.empty() || text[0] == '-') throw std::runtime_error("Numero invalido.");
    size_t end = 0;
    unsigned long long value = std::stoull(text, &end,
        text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10);
    if (end != text.size() || value > 0xFFFFFFFFull) throw std::runtime_error("Numero fuera de rango.");
    return uint32_t(value);
}

unsigned register_index(const std::string& name) {
    if (name == "fp") return 8;
    for (unsigned i = 0; i < 32; ++i) if (name == names[i]) return i;
    if (name.size() > 1 && name[0] == 'x') {
        uint32_t i = number(name.substr(1));
        if (i < 32) return i;
    }
    throw std::runtime_error("Registro invalido: " + name);
}

std::string hex32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

long long signed_value(uint32_t value) {
    return (value & 0x80000000u) ? static_cast<long long>(value) - 4294967296LL : value;
}

void print_reg(const Simulator& sim, unsigned i) {
    uint32_t value = sim.get_reg(i);
    std::cout << 'x' << i << " (" << names[i] << ") = " << hex32(value)
              << "  unsigned=" << value << "  signed=" << signed_value(value) << '\n';
}

void help() {
    std::cout <<
        "help                     Muestra esta ayuda\n"
        "step [n]                 Ejecuta n instrucciones (por defecto 1)\n"
        "run [limite]             Ejecuta hasta parar o llegar al limite (1000000)\n"
        "pc                       Muestra PC e instruccion actual en hexadecimal\n"
        "regs [x5 a0 ...]         Muestra todos los registros o los indicados\n"
        "mem inicio fin           Muestra bytes; ambas direcciones incluidas\n"
        "word direccion [n]       Muestra n palabras de 32 bits (por defecto 1)\n"
        "reset                    Recarga el programa y pone PC y registros en cero\n"
        "load ruta del archivo    Carga otro binario crudo desde cero\n"
        "exit                     Sale del simulador\n"
        "Numeros: decimal o hexadecimal con prefijo 0x.\n";
}

int main(int argc, char** argv) {
    Simulator sim;
    if (argc > 2) { std::cerr << "Uso: simulador [programa.bin]\n"; return 1; }
    bool loaded = false;
    if (argc == 2) {
        if (!sim.load_from_file(argv[1])) { std::cerr << sim.get_message() << '\n'; return 1; }
        loaded = true;
        std::cout << "Programa cargado en 0x00000000.\n";
    }
    std::cout << "Simulador RISC-V RV32I. Escribe help para ver los comandos.\n";
    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command, arg, extra;
        input >> command;
        if (command.empty()) continue;
        try {
            if (command == "exit" || command == "quit") break;
            if (command == "help") { help(); continue; }
            if (command == "load") {
                std::getline(input >> std::ws, arg);
                if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') arg = arg.substr(1, arg.size() - 2);
                if (arg.empty()) throw std::runtime_error("Uso: load ruta del archivo");
                if (!sim.load_from_file(arg)) throw std::runtime_error(sim.get_message());
                loaded = true;
                std::cout << "Programa cargado en 0x00000000.\n";
                continue;
            }
            if (!loaded) throw std::runtime_error("Primero carga un programa con load.");
            if (command == "pc") {
                if (input >> extra) throw std::runtime_error("Uso: pc");
                std::cout << "PC = " << hex32(sim.get_pc()) << ", instruccion = "
                          << hex32(sim.read32(sim.get_pc())) << '\n';
            } else if (command == "regs") {
                if (!(input >> arg)) for (unsigned i = 0; i < 32; ++i) print_reg(sim, i);
                else do { print_reg(sim, register_index(arg)); } while (input >> arg);
            } else if (command == "mem") {
                std::string end_text;
                if (!(input >> arg >> end_text) || (input >> extra)) throw std::runtime_error("Uso: mem inicio fin");
                uint32_t start = number(arg), end = number(end_text);
                if (end < start || uint64_t(end) - start + 1 > 4096) throw std::runtime_error("Rango invalido; maximo 4096 bytes por consulta.");
                for (uint64_t i = 0; i <= uint64_t(end) - start; ++i) {
                    if (i % 16 == 0) std::cout << hex32(start + uint32_t(i)) << ": ";
                    std::cout << std::hex << std::setw(2) << std::setfill('0')
                              << unsigned(sim.read8(start + uint32_t(i))) << std::dec << ' ';
                    if (i % 16 == 15 || i == uint64_t(end) - start) std::cout << '\n';
                }
            } else if (command == "word") {
                if (!(input >> arg)) throw std::runtime_error("Uso: word direccion [cantidad]");
                uint32_t start = number(arg), count = 1;
                if (input >> arg) count = number(arg);
                if ((input >> extra) || count == 0 || count > 1024 ||
                    uint64_t(start) + uint64_t(count) * 4 > 0x100000000ull)
                    throw std::runtime_error("Rango invalido; maximo 1024 palabras.");
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t address = start + i * 4, value = sim.read32(address);
                    std::cout << hex32(address) << ": " << hex32(value) << " (" << signed_value(value) << ")\n";
                }
            } else if (command == "reset") {
                if (input >> extra) throw std::runtime_error("Uso: reset");
                sim.reset(); std::cout << "Programa reiniciado.\n";
            } else if (command == "step" || command == "run") {
                uint32_t count = command == "step" ? 1 : 1000000;
                if (input >> arg) count = number(arg);
                if ((input >> extra) || count == 0) throw std::runtime_error("Indica una cantidad positiva.");
                uint64_t executed = sim.run(count);
                std::cout << "Ejecutadas: " << executed << ". PC = " << hex32(sim.get_pc())
                          << ". Total: " << sim.get_steps() << '\n';
                if (!sim.get_message().empty() && (sim.is_halted() || command == "run")) std::cout << sim.get_message() << '\n';
            } else std::cout << "Comando desconocido. Escribe help.\n";
        } catch (const std::exception& error) {
            std::cout << "Error: " << error.what() << '\n';
        }
    }
    return 0;
}
