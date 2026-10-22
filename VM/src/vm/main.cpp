#include "lcero/vm/cli.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <new>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {

std::filesystem::path executable_path(const wchar_t* argv_zero) {
#ifdef _WIN32
    // GetModuleFileNameW obtiene la ubicacion real aunque la VM se invoque
    // desde otro CWD o mediante PATH. Esa base solo se usa como alternativa
    // para localizar una RI; nunca forma parte del lenguaje LCERO_IR 1.
    std::wstring buffer(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0U &&
        static_cast<std::size_t>(length) < buffer.size()) {
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }
#endif
    return argv_zero == nullptr ? std::filesystem::path{}
                                : std::filesystem::path(argv_zero);
}

}  // namespace

// wmain conserva cada argumento como Unicode en Windows. Esto evita perder
// informacion al representar una ruta .lci con espacios o caracteres Unicode.
int wmain(const int argc, wchar_t* argv[]) {
#ifdef _WIN32
    // Windows traduce '\n' a CRLF en modo texto. La salida observable O de la
    // maquina exige bytes LF aun redirigidos, por eso stdout se abre binario.
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::wcerr << L"LCERO_CLI_ERROR=RECURSO "
                      L"no se pudo configurar stdout en modo binario\n";
        return static_cast<int>(
            lcero::vm::cli::ExitCode::usage_or_file_error);
    }
#endif

    try {
        std::vector<std::wstring> arguments;
        arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);

        // argv[0] identifica al ejecutable y no forma parte de la gramatica de
        // la CLI. Los demas lexemas se entregan en orden al analizador.
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }

        return lcero::vm::cli::run(
            arguments, std::cout, std::wcerr, executable_path(argv[0]));
    } catch (const std::bad_alloc&) {
        // Si el proceso no puede representar siquiera los argumentos, no
        // existe una ejecucion valida de C que pueda declararse HALT.
        std::wcerr << L"LCERO_CLI_ERROR=RECURSO CODE=RECURSO_AGOTADO "
                      L"MESSAGE=memoria insuficiente en la CLI\n";
        return static_cast<int>(
            lcero::vm::cli::ExitCode::usage_or_file_error);
    } catch (const std::exception& exception) {
        std::wcerr << L"LCERO_CLI_ERROR=INTERNO MESSAGE="
                   << exception.what() << L'\n';
        return static_cast<int>(
            lcero::vm::cli::ExitCode::usage_or_file_error);
    }
}
