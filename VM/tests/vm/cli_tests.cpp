#include "lcero/vm/cli.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <system_error>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char* description) {
    if (condition) {
        std::cout << "[OK] " << description << '\n';
        return;
    }
    ++failures;
    std::cerr << "[FALLO] " << description << '\n';
}

struct CliResult {
    int exit_code{};
    std::string standard_output;
    std::wstring standard_error;
};

class RejectingOutputBuffer final : public std::streambuf {
protected:
    // Este doble de prueba representa un recurso de salida que deja de aceptar
    // bytes. Permite comprobar la categoria RECURSOS sin agotar memoria real.
    std::streamsize xsputn(const char*, std::streamsize) override {
        return 0;
    }

    int_type overflow(int_type) override {
        return traits_type::eof();
    }
};

CliResult invoke(const std::vector<std::wstring>& arguments) {
    std::ostringstream output;
    std::wostringstream error;
    const int exit_code = lcero::vm::cli::run(arguments, output, error);
    return {exit_code, output.str(), error.str()};
}

CliResult invoke_from_executable(
    const std::vector<std::wstring>& arguments,
    const std::filesystem::path& executable_path) {
    std::ostringstream output;
    std::wostringstream error;
    const int exit_code = lcero::vm::cli::run(
        arguments, output, error, executable_path);
    return {exit_code, output.str(), error.str()};
}

std::filesystem::path temporary_lci(const wchar_t* name,
                                    const std::string& contents) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / name;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    expect(file.good(), "se crea el fixture temporal de CLI");
    return path;
}

void remove_fixture(const std::filesystem::path& path) {
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    expect(!cleanup_error, "se elimina unicamente el fixture temporal propio");
}

void test_boost_dependency() {
    // cpp_int representa Z sin fijarlo a 32 o 64 bits, tal como exige el
    // dominio matematico de registros, memoria e inmediatos de L-Cero.
    boost::multiprecision::cpp_int large_integer = 1;
    large_integer <<= 100;
    expect(large_integer > 0, "Boost.Multiprecision cpp_int disponible");
}

void test_help_and_version() {
    const CliResult help = invoke({L"--help"});
    expect(help.exit_code == 0, "--help finaliza con codigo 0");
    expect(help.standard_output.find("--max-steps") != std::string::npos,
           "--help documenta el presupuesto de transiciones");
    expect(help.standard_output.find("--no-step-limit") != std::string::npos,
           "--help documenta la ejecucion sin limite explicita");
    expect(help.standard_error.empty(), "--help no emite diagnosticos");

    const CliResult version = invoke({L"--version"});
    expect(version.exit_code == 0, "--version finaliza con codigo 0");
    expect(version.standard_output == "lcero-vm 0.7.0 (etapa 07)\n",
           "--version identifica la etapa ejecutable");
}

void test_invalid_arguments() {
    const CliResult unknown = invoke({L"--opcion-inexistente"});
    expect(unknown.exit_code == 5, "una opcion desconocida devuelve codigo 5");
    expect(unknown.standard_error.find(L"LCERO_CLI_ERROR=ARGUMENTOS") !=
               std::wstring::npos,
           "la opcion desconocida produce diagnostico categorizado");

    const CliResult incompatible =
        invoke({L"--max-steps", L"1", L"--no-step-limit", L"programa.lci"});
    expect(incompatible.exit_code == 5,
           "dos politicas de limite incompatibles se rechazan");

    const CliResult negative =
        invoke({L"--max-steps", L"-1", L"programa.lci"});
    expect(negative.exit_code == 5,
           "--max-steps -1 se rechaza como argumento y no como LIMIT");

    const CliResult non_numeric =
        invoke({L"--max-steps", L"muchos", L"programa.lci"});
    expect(non_numeric.exit_code == 5,
           "un presupuesto no numerico se rechaza como argumento");

    const CliResult missing_path = invoke({L"--max-steps", L"3"});
    expect(missing_path.exit_code == 5,
           "una opcion valida sin ruta no inicia la maquina");

    const CliResult missing_trace_path = invoke({L"--trace"});
    expect(missing_trace_path.exit_code == 5,
           "--trace sin ruta de log se rechaza como argumento");

    const CliResult repeated_trace = invoke(
        {L"--trace", L"uno.log", L"--trace", L"dos.log", L"programa.lci"});
    expect(repeated_trace.exit_code == 5,
           "--trace no puede repetirse en una ejecucion");

    const CliResult check_with_trace = invoke(
        {L"--check", L"--trace", L"salida.log", L"programa.lci"});
    expect(check_with_trace.exit_code == 5,
           "--check rechaza una traza porque no ejecuta transiciones");

    const CliResult trace_as_ir = invoke(
        {L"--trace", L"no_es_un_log.lci", L"programa.lci"});
    expect(trace_as_ir.exit_code == 5,
           "la traza no puede confundirse con un segundo archivo de RI");

    const std::filesystem::path absent =
        std::filesystem::temp_directory_path() /
        L"lcero_ruta_inexistente_etapa04.lci";
    const CliResult nonexistent = invoke({absent.wstring()});
    expect(nonexistent.exit_code == 5,
           "una ruta inexistente no se presenta como HALT");
    expect(nonexistent.standard_output.empty(),
           "una ruta inexistente no produce PRINT");
}

void test_default_source_from_vm_project() {
    const auto unique_name = "lcero_cli_default_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path project =
        std::filesystem::temp_directory_path() / unique_name;
    const std::filesystem::path source = project / "Source";
    const std::filesystem::path program = source / "programa.lci";
    const std::filesystem::path executable =
        project / "build" / "bin" / "Debug" / "lcero-vm.exe";

    std::error_code file_error;
    std::filesystem::create_directories(source, file_error);
    expect(!file_error, "se crea el proyecto temporal de la VM");
    {
        std::ofstream cmake(project / "CMakeLists.txt");
        std::ofstream manifest(project / "vcpkg.json");
        std::ofstream input(program, std::ios::binary);
        input << "LCERO_IR 1\nMEM 0\nCONST R0, 17\nPRINT R0\nHALT\n";
        expect(cmake.good() && manifest.good() && input.good(),
               "se crea Source/programa.lci junto a la raiz de la VM");
    }

    const CliResult result = invoke_from_executable({}, executable);
    expect(result.exit_code == 0 && result.standard_output == "17\n",
           "sin argumentos se ejecuta el programa de Source de la VM");
    expect(result.standard_error.find(L"LCERO_STATUS=HALT") !=
               std::wstring::npos,
           "la ejecucion predeterminada informa HALT");

    std::filesystem::remove(program, file_error);
    expect(!file_error, "se retira el programa temporal");
    const CliResult missing = invoke_from_executable({}, executable);
    expect(missing.exit_code == 5 && missing.standard_output.empty() &&
               missing.standard_error.find(L"LCERO_CLI_ERROR=ARCHIVO") !=
                   std::wstring::npos,
           "sin programa predeterminado se informa el archivo faltante");

    std::filesystem::remove(project / "CMakeLists.txt", file_error);
    std::filesystem::remove(project / "vcpkg.json", file_error);
    std::filesystem::remove(source, file_error);
    std::filesystem::remove(project, file_error);
    expect(!file_error, "se elimina la jerarquia temporal creada por la prueba");
}

void test_execution_with_unicode_path() {
    // wmain y filesystem conservan la ruta Unicode. El texto interno de la RI
    // sigue siendo ASCII, como impone el contrato LCERO_IR 1.
    const std::filesystem::path input = temporary_lci(
        L"lcero_ejecucion_\u00E1.lci",
        "LCERO_IR 1\nMEM 0\nCONST R0, 5\nPRINT R0\nHALT\n");

    const CliResult result = invoke({input.wstring()});
    expect(result.exit_code == 0, "un programa valido alcanza HALT");
    expect(result.standard_output == "5\n",
           "stdout contiene exclusivamente PRINT con LF");
    expect(result.standard_error ==
               L"LCERO_STATUS=HALT LCERO_STEPS=3 PC=2 "
               L"CATEGORY=TERMINACION\n",
           "stderr informa el estado C final sin contaminar PRINT");

    remove_fixture(input);
}

void test_relative_path_falls_back_to_executable_tree() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        L"lcero_cli_ruta_relativa_desde_ejecutable";
    const std::filesystem::path tests = root / L"TestsParaVM";
    const std::filesystem::path executable =
        root / L"build" / L"bin" / L"Debug" / L"lcero-vm.exe";
    const std::filesystem::path program = tests / L"portabilidad.lci";

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    cleanup_error.clear();
    std::filesystem::create_directories(tests, cleanup_error);
    expect(!cleanup_error,
           "se crea la jerarquia portable junto al ejecutable simulado");
    {
        std::ofstream file(program, std::ios::binary | std::ios::trunc);
        file << "LCERO_IR 1\nMEM 0\nCONST R0, 17\nPRINT R0\nHALT\n";
        expect(file.good(), "se crea la RI fuera del directorio de trabajo");
    }

    // La ruta relativa no existe en el CWD del test. La CLI debe conservar
    // primero esa regla y luego resolverla desde los ancestros del .exe.
    const CliResult result = invoke_from_executable(
        {L"TestsParaVM\\portabilidad.lci"}, executable);
    expect(result.exit_code == 0,
           "la VM encuentra una RI relativa desde su arbol de instalacion");
    expect(result.standard_output == "17\n",
           "la resolucion alternativa no cambia la salida PRINT");
    expect(result.standard_error.find(L"LCERO_STATUS=HALT") !=
               std::wstring::npos,
           "la RI localizada conserva su terminacion operacional");

    cleanup_error.clear();
    std::filesystem::remove_all(root, cleanup_error);
    expect(!cleanup_error,
           "se elimina unicamente la jerarquia temporal portable");
}

void test_check_mode() {
    const std::filesystem::path input = temporary_lci(
        L"lcero_check_\u00F1.lci", "LCERO_IR 1\nMEM 0\nHALT\n");
    const CliResult valid = invoke({L"--check", input.wstring()});
    expect(valid.exit_code == 0, "--check acepta una RI valida");
    expect(valid.standard_output.empty(), "--check no ejecuta PRINT");
    expect(valid.standard_error == L"LCERO_CHECK=VALID\n",
           "--check no simula HALT ni pasos");

    {
        std::ofstream file(input, std::ios::binary | std::ios::trunc);
        file << "LCERO_IR 2\nMEM 0\nHALT\n";
    }
    const CliResult invalid = invoke({L"--check", input.wstring()});
    expect(invalid.exit_code == 3,
           "--check devuelve codigo 3 para ERROR_DE_CARGA");
    expect(invalid.standard_output.empty(),
           "ERROR_DE_CARGA no contamina stdout");
    expect(invalid.standard_error.find(L"LINE=1 CODE=VERSION_INVALIDA") !=
               std::wstring::npos,
           "el diagnostico de carga incluye linea y causa");
    expect(invalid.standard_error.find(
               L"LCERO_STEPS=0 PC=0 CATEGORY=CARGA") != std::wstring::npos,
           "ERROR_DE_CARGA certifica que no hubo transiciones");

    remove_fixture(input);
    const CliResult inaccessible = invoke({L"--check", input.wstring()});
    expect(inaccessible.exit_code == 5,
           "--check distingue un archivo inaccesible con codigo 5");
}

void test_limits_and_runtime_error() {
    const std::filesystem::path halt = temporary_lci(
        L"lcero_limite.lci", "LCERO_IR 1\nMEM 0\nHALT\n");
    const CliResult zero =
        invoke({halt.wstring(), L"--max-steps", L"0"});
    expect(zero.exit_code == 4, "N=0 produce LIMIT antes de HALT");
    expect(zero.standard_error ==
               L"LCERO_STATUS=LIMIT LCERO_STEPS=0 PC=0 "
               L"CATEGORY=PRESUPUESTO\n",
           "LIMIT conserva k=0 y pc=0");

    const CliResult unlimited = invoke({L"--no-step-limit", halt.wstring()});
    expect(unlimited.exit_code == 0,
           "--no-step-limit permite ejecutar un programa finito");
    remove_fixture(halt);

    const std::filesystem::path error = temporary_lci(
        L"lcero_error_dinamico.lci",
        "LCERO_IR 1\nMEM 0\nCONST R0, 8\nPRINT R0\nCONST R1, 0\n"
        "DIV R2, R0, R1\nHALT\n");
    const CliResult division = invoke({error.wstring()});
    expect(division.exit_code == 2,
           "DIV por cero produce ERROR dinamico con codigo 2");
    expect(division.standard_output == "8\n",
           "ERROR conserva la salida PRINT previa en orden");
    expect(division.standard_error.find(L"CODE=DIVISION_POR_CERO") !=
               std::wstring::npos,
           "el error dinamico informa su clase y linea");
    expect(division.standard_error.find(L"CATEGORY=EJECUCION") !=
               std::wstring::npos,
           "el error dinamico se distingue de carga y recursos");

    const std::filesystem::path output_failure = temporary_lci(
        L"lcero_salida_no_disponible.lci",
        "LCERO_IR 1\nMEM 0\nCONST R0, 9\nPRINT R0\nHALT\n");
    RejectingOutputBuffer rejecting_buffer;
    std::ostream unavailable_output(&rejecting_buffer);
    std::wostringstream resource_error;
    const int resource_exit = lcero::vm::cli::run(
        {output_failure.wstring()}, unavailable_output, resource_error);
    expect(resource_exit == 2,
           "un fallo fisico de stdout no se declara ejecucion exitosa");
    expect(resource_error.str().find(L"CATEGORY=RECURSOS") !=
               std::wstring::npos,
           "el fallo fisico se clasifica como RECURSOS y no como sintaxis");
    remove_fixture(output_failure);
    remove_fixture(error);
}

void test_state_is_reinitialized_between_runs() {
    const std::filesystem::path writer = temporary_lci(
        L"lcero_reinicio_escribe.lci",
        "LCERO_IR 1\nMEM 0\nCONST R0, 99\nHALT\n");
    const std::filesystem::path reader = temporary_lci(
        L"lcero_reinicio_lee.lci",
        "LCERO_IR 1\nMEM 0\nPRINT R0\nHALT\n");

    const CliResult first = invoke({writer.wstring()});
    const CliResult second = invoke({reader.wstring()});

    expect(first.exit_code == 0 && first.standard_output.empty(),
           "la primera VM puede inicializar R0 y alcanzar HALT");
    // Cada archivo construye una configuracion C nueva. Si R fuera estado
    // global compartido, el segundo PRINT observaria 99, violando R vacio.
    expect(second.exit_code == 2 && second.standard_output.empty() &&
               second.standard_error.find(L"REGISTRO_NO_INICIALIZADO") !=
                   std::wstring::npos,
           "una segunda ejecucion comienza con registros no inicializados");

    remove_fixture(writer);
    remove_fixture(reader);
}

}  // namespace

int main() {
    test_boost_dependency();
    test_help_and_version();
    test_invalid_arguments();
    test_default_source_from_vm_project();
    test_execution_with_unicode_path();
    test_relative_path_falls_back_to_executable_tree();
    test_check_mode();
    test_limits_and_runtime_error();
    test_state_is_reinitialized_between_runs();

    if (failures == 0) {
        std::cout << "Todas las pruebas de CLI hasta la etapa 07 pasaron.\n";
        return 0;
    }
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
}
