#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace lcero::vm::cli {

// Estos codigos pertenecen a la interfaz del proceso de Windows. No son
// estados ni simbolos del lenguaje LCERO_IR 1, cuyo contrato permanece intacto.
enum class ExitCode : int {
    success = 0,
    execution_error = 2,
    load_error = 3,
    step_limit = 4,
    usage_or_file_error = 5,
};

// Recibe argumentos ya separados para probar la gramatica de la CLI sin iniciar
// otro proceso. stdout es de bytes: PRINT debe serializar LF sin traduccion.
int run(const std::vector<std::wstring>& arguments,
        std::ostream& standard_output,
        std::wostream& standard_error,
        const std::filesystem::path& executable_path = {});

}  // namespace lcero::vm::cli
