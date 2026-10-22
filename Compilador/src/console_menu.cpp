#include "lcero/console_menu.hpp"

#include <string>

namespace lcero {

MenuResult InteractiveMenu::prompt(
    std::istream& input,
    std::ostream& output,
    const std::filesystem::path& input_path) const {
    output << "========================================\n"
           << "             L-CERO COMPILER\n"
           << "========================================\n\n"
           << "Archivo de entrada: " << input_path.string() << "\n\n";

    for (;;) {
        output << "Seleccione el modo de compilacion:\n\n"
               << "  [1] Compilacion normal (sin optimizacion)\n"
               << "  [2] Compilacion optimizada (Chaitin)\n\n"
               << "  [0] Salir\n\n"
               << "Opcion: ";
        std::string option;
        if (!std::getline(input, option)) {
            output << "\nEntrada finalizada. No se realizo la compilacion.\n";
            return MenuResult{MenuAction::EndOfInput, std::nullopt};
        }
        if (option == "0") {
            output << "No se realizo la compilacion.\n";
            return MenuResult{MenuAction::Exit, std::nullopt};
        }
        if (option == "1") {
            return MenuResult{MenuAction::Compile, BackendKind::Reference};
        }
        if (option == "2") {
            return MenuResult{MenuAction::Compile, BackendKind::Optimized};
        }
        output << "Opcion invalida. Ingrese 0, 1 o 2.\n\n";
    }
}

} // namespace lcero
