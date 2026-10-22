#include "lcero/vm/cli.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string& description) {
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

CliResult invoke(const std::vector<std::wstring>& arguments) {
    std::ostringstream output;
    std::wostringstream error;
    const int exit_code = lcero::vm::cli::run(arguments, output, error);
    return {exit_code, output.str(), error.str()};
}

std::string read_binary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::filesystem::path make_temporary_directory() {
    const auto suffix = std::chrono::high_resolution_clock::now()
                            .time_since_epoch()
                            .count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("lcero trace tests " + std::to_string(suffix));
    std::error_code error;
    const bool created = std::filesystem::create_directory(directory, error);
    expect(created && !error,
           "se crea un directorio temporal propio con espacios");
    return directory;
}

void remove_file(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    expect(!error, "se elimina unicamente un log temporal propio");
}

void test_same_observable_execution(const std::filesystem::path& contracts,
                                    const std::filesystem::path& temporary) {
    const std::filesystem::path program =
        contracts / "valid/01_hola_registros.lci";
    const std::filesystem::path trace = temporary / "hola con traza.log";

    const CliResult without_trace = invoke({program.wstring()});
    const CliResult with_trace =
        invoke({L"--trace", trace.wstring(), program.wstring()});

    // Una traza es un observador: no forma parte de O y no puede modificar
    // HALT, pc ni k de la maquina abstracta C=(P,pc,R,M,O,k).
    expect(with_trace.exit_code == without_trace.exit_code,
           "activar trace conserva el codigo de salida");
    expect(with_trace.standard_output == without_trace.standard_output &&
               with_trace.standard_output == "5\n",
           "activar trace conserva exactamente los bytes PRINT");
    expect(with_trace.standard_error == without_trace.standard_error &&
               with_trace.standard_error.find(L"LCERO_STEPS=3 PC=2") !=
                   std::wstring::npos,
           "activar trace conserva estado terminal, pc y k");

    const std::string contents = read_binary(trace);
    expect(contents.find("LCERO_TRACE 1\nFORMAT mode=DIFF") == 0U,
           "el log declara version y politica de diferencias");
    expect(contents.find("INITIAL k=0 pc=0 status=RUNNING") !=
               std::string::npos,
           "el encabezado registra el estado inicial vacio");
    expect(contents.find(
               "BEFORE k=0 pc=0 exec_index=0 source_line=3 opcode=CONST") !=
               std::string::npos,
           "BEFORE relaciona pc ejecutable con linea fuente");
    expect(contents.find("AFTER k=1 pc=1 exec_index=0 source_line=3 "
                         "opcode=CONST status=RUNNING diff=[R0=5]") !=
               std::string::npos,
           "AFTER informa el cambio de registro relevante");
    expect(contents.find("diff=[output_append=5\\n]") != std::string::npos,
           "PRINT queda en la traza sin escribir el log en stdout");
    expect(contents.find("TERMINAL status=HALT k=3 pc=2") !=
               std::string::npos,
           "HALT queda trazado con su paso contado");
    expect(contents.find('\r') == std::string::npos,
           "el archivo binario de traza usa LF sin bytes CR");

    remove_file(trace);
}

void test_loop_and_label_mapping(const std::filesystem::path& contracts,
                                 const std::filesystem::path& temporary) {
    const std::filesystem::path program =
        contracts / "valid/08_bucle_descendente.lci";
    const std::filesystem::path trace = temporary / "bucle.log";
    const CliResult result =
        invoke({program.wstring(), L"--trace", trace.wstring()});
    const std::string contents = read_binary(trace);

    expect(result.exit_code == 0 && result.standard_output == "3\n2\n1\n",
           "el bucle trazado conserva los tres PRINT en orden");
    expect(result.standard_error.find(L"LCERO_STEPS=40") !=
               std::wstring::npos,
           "el bucle trazado alcanza HALT con k=40");
    expect(contents.find("LABEL name=ciclo pc=2") != std::string::npos &&
               contents.find("LABEL name=fin pc=13") != std::string::npos,
           "el encabezado conserva la tabla LABEL a pc");
    expect(contents.find("opcode=JMP") != std::string::npos &&
               contents.find("opcode=JZ") != std::string::npos &&
               contents.find("diff=[pc:") != std::string::npos,
           "la traza hace visibles saltos condicionales e incondicionales");
    expect(contents.find("TERMINAL status=HALT k=40") != std::string::npos,
           "el terminal del bucle coincide con la semantica operacional");

    remove_file(trace);
}

void test_limit_and_runtime_error(const std::filesystem::path& contracts,
                                  const std::filesystem::path& temporary) {
    const std::filesystem::path loop =
        contracts / "limit/17_bucle_bajo_limite.lci";
    const std::filesystem::path limit_trace = temporary / "limite.log";
    // Este fixture se ejecuta exclusivamente con presupuesto: LIMIT observa
    // k=4, pero no constituye una prueba del problema de la parada.
    const CliResult limited = invoke(
        {L"--max-steps", L"4", L"--trace", limit_trace.wstring(),
         loop.wstring()});
    const std::string limit_contents = read_binary(limit_trace);
    expect(limited.exit_code == 4 && limited.standard_output.empty() &&
               limited.standard_error.find(L"LCERO_STEPS=4") !=
                   std::wstring::npos,
           "el bucle protegido devuelve LIMIT con k=4");
    expect(limit_contents.find("TERMINAL status=LIMIT k=4") !=
               std::string::npos &&
               limit_contents.find("LIMIT_IS_NOT_NONTERMINATION_PROOF") !=
                   std::string::npos,
           "la traza documenta presupuesto agotado sin inferir no terminacion");

    const std::filesystem::path failing =
        contracts / "runtime_error/24_salida_parcial_antes_error.lci";
    const std::filesystem::path error_trace = temporary / "error.log";
    const CliResult runtime_error =
        invoke({L"--trace", error_trace.wstring(), failing.wstring()});
    const std::string error_contents = read_binary(error_trace);
    expect(runtime_error.exit_code == 2 &&
               runtime_error.standard_output == "8\n",
           "ERROR conserva la salida parcial aun con trace activo");
    expect(error_contents.find("opcode=DIV status=ERROR diff=[none] "
                               "diagnostic=DIVISION_POR_CERO") !=
               std::string::npos,
           "la instruccion fallida no recibe un incremento ni efecto ficticio");
    expect(error_contents.find("TERMINAL status=ERROR k=3") !=
               std::string::npos,
           "la traza parcial sobrevive al error dinamico");

    remove_file(limit_trace);
    remove_file(error_trace);
}

void test_inaccessible_trace(const std::filesystem::path& contracts,
                             const std::filesystem::path& temporary) {
    const std::filesystem::path program =
        contracts / "valid/01_hola_registros.lci";
    const std::filesystem::path impossible =
        temporary / "directorio_que_no_existe" / "salida.log";
    const CliResult result =
        invoke({L"--trace", impossible.wstring(), program.wstring()});

    expect(result.exit_code == 5,
           "una ruta de traza inaccesible devuelve el codigo de CLI 5");
    expect(result.standard_output.empty(),
           "si el log no puede abrirse no se ejecuta ningun PRINT");
    expect(result.standard_error.find(
               L"LCERO_CLI_ERROR=TRAZA CODE=TRAZA_INACCESIBLE") !=
               std::wstring::npos,
           "el fallo de apertura del log tiene categoria explicita");
}

void measure_trace_overhead(const std::filesystem::path& contracts,
                            const std::filesystem::path& temporary) {
    const std::filesystem::path program =
        contracts / "valid/08_bucle_descendente.lci";
    const std::filesystem::path trace = temporary / "medicion.log";

    const auto plain_start = std::chrono::steady_clock::now();
    const CliResult plain = invoke({program.wstring()});
    const auto plain_end = std::chrono::steady_clock::now();
    const CliResult observed =
        invoke({L"--trace", trace.wstring(), program.wstring()});
    const auto observed_end = std::chrono::steady_clock::now();

    const auto plain_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              plain_end - plain_start)
                              .count();
    const auto trace_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              observed_end - plain_end)
                              .count();
    expect(plain.exit_code == 0 && observed.exit_code == 0,
           "la medicion compara dos ejecuciones completas");
    std::cout << "[MEDICION] 08_bucle_descendente sin_trace_us=" << plain_us
              << " con_trace_us=" << trace_us
              << " (dato orientativo; no es un umbral de aceptacion)\n";

    remove_file(trace);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Uso: lcero_vm_trace_tests <test/contracts>\n";
        return 2;
    }

    const std::filesystem::path contracts = argv[1];
    const std::filesystem::path temporary = make_temporary_directory();
    test_same_observable_execution(contracts, temporary);
    test_loop_and_label_mapping(contracts, temporary);
    test_limit_and_runtime_error(contracts, temporary);
    test_inaccessible_trace(contracts, temporary);
    measure_trace_overhead(contracts, temporary);

    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    expect(!cleanup_error,
           "se elimina el directorio temporal propio cuando queda vacio");

    if (failures == 0) {
        std::cout << "Todas las pruebas de trazas de la etapa 05 pasaron.\n";
        return 0;
    }
    std::cerr << failures << " prueba(s) de trazas fallaron.\n";
    return 1;
}
