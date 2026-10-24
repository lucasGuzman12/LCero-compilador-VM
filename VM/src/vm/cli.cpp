#include "lcero/vm/cli.hpp"
#include "lcero/vm/diagnostics.hpp"
#include "lcero/vm/interpreter.hpp"
#include "lcero/vm/ir_loader.hpp"
#include "lcero/vm/ir_validator.hpp"
#include "lcero/vm/trace.hpp"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <new>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace lcero::vm::cli {
namespace {

constexpr char kVersion[] = "lcero-vm 0.7.0 (etapa 07)";

struct ParsedCommand {
    bool check_mode = false;
    std::filesystem::path input_path;
    std::optional<std::filesystem::path> trace_path;
    ExecutionOptions execution_options;
};

int as_integer(const ExitCode code) {
    return static_cast<int>(code);
}

void print_help(std::ostream& output) {
    // La extension .lci marca la frontera formal entre compilador y VM: esta
    // interfaz nunca acepta fuente .lc, AST ni tokens del lexer del compilador.
    output << "Uso:\n"
           << "  lcero-vm.exe                         (Source/programa.lci)\n"
           << "  lcero-vm.exe --help\n"
           << "  lcero-vm.exe --version\n"
           << "  lcero-vm.exe --check ruta\\programa.lci\n"
           << "  lcero-vm.exe [--max-steps N | --no-step-limit] "
              "[--trace ruta\\archivo.log] ruta\\programa.lci\n\n"
           << "Ejecuta LCERO_IR 1 con limite predeterminado de 100000 pasos.\n"
           << "--check valida sin ejecutar; --max-steps 0 es valido.\n"
           << "--trace escribe un log UTF-8/LF sin mezclarlo con PRINT.\n"
           << "Sin argumentos ejecuta Source/programa.lci del proyecto VM.\n"
           << "Una ruta relativa explicita busca primero en CWD y luego junto "
              "al arbol del ejecutable.\n";
}

void print_cli_error(std::wostream& error,
                     const wchar_t* category,
                     const std::wstring& detail) {
    // LCERO_CLI_ERROR pertenece al proceso anfitrion, no a los estados de la
    // maquina abstracta C=(P,pc,R,M,O,k).
    error << L"LCERO_CLI_ERROR=" << category << L" " << detail << L'\n';
}

bool has_lci_extension(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const wchar_t character) {
                       return static_cast<wchar_t>(std::towlower(character));
                   });
    return extension == L".lci";
}

std::filesystem::path resolve_input_path(
    const std::filesystem::path& requested_path,
    const std::filesystem::path& executable_path) {
    // Una ruta absoluta ya expresa por completo la intencion del usuario.
    // Para rutas relativas se conserva primero la convencion historica del
    // CWD. Solo si no existe alli se consulta la jerarquia del ejecutable.
    if (requested_path.is_absolute()) {
        return requested_path;
    }

    std::error_code status_error;
    if (std::filesystem::is_regular_file(requested_path, status_error) &&
        !status_error) {
        return requested_path;
    }
    if (executable_path.empty()) {
        return requested_path;
    }

    std::error_code absolute_error;
    const std::filesystem::path absolute_executable =
        std::filesystem::absolute(executable_path, absolute_error);
    if (absolute_error) {
        return requested_path;
    }

    // En una construccion Visual Studio el ejecutable vive normalmente en
    // <raiz>/build/bin/Debug. Ascender permite encontrar, por ejemplo,
    // <raiz>/TestsParaVM/prueba_1.lci sin codificar una ruta personal.
    std::filesystem::path directory = absolute_executable.parent_path();
    while (!directory.empty()) {
        const std::filesystem::path candidate = directory / requested_path;
        std::error_code candidate_error;
        if (std::filesystem::is_regular_file(candidate, candidate_error) &&
            !candidate_error) {
            return candidate;
        }

        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }
    return requested_path;
}

std::optional<std::filesystem::path> find_vm_project_root(
    std::filesystem::path directory) {
    std::error_code absolute_error;
    directory = std::filesystem::absolute(directory, absolute_error);
    if (absolute_error) {
        return std::nullopt;
    }

    while (!directory.empty()) {
        std::error_code cmake_error;
        std::error_code manifest_error;
        if (std::filesystem::is_regular_file(directory / "CMakeLists.txt",
                                             cmake_error) &&
            !cmake_error &&
            std::filesystem::is_regular_file(directory / "vcpkg.json",
                                             manifest_error) &&
            !manifest_error) {
            return directory;
        }

        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }
    return std::nullopt;
}

std::filesystem::path default_input_path(
    const std::filesystem::path& executable_path) {
    const std::filesystem::path relative_source =
        std::filesystem::path("Source") / "programa.lci";
    if (!executable_path.empty()) {
        const auto root = find_vm_project_root(executable_path.parent_path());
        if (root.has_value()) {
            return *root / relative_source;
        }
    }

    std::error_code current_path_error;
    const auto current_directory =
        std::filesystem::current_path(current_path_error);
    if (!current_path_error) {
        const auto root = find_vm_project_root(current_directory);
        if (root.has_value()) {
            return *root / relative_source;
        }
    }
    return relative_source;
}

std::wstring widen_ascii(const std::string_view text) {
    return std::wstring(text.begin(), text.end());
}

bool is_physical_input_failure(const Diagnostic& diagnostic) {
    return diagnostic.category == DiagnosticCategory::resource ||
           diagnostic.code == DiagnosticCode::file_open_failed ||
           diagnostic.code == DiagnosticCode::file_read_failed;
}

void print_load_diagnostic(std::wostream& error,
                           const Diagnostic& diagnostic) {
    // La validacion total precede a C. Los ceros observables certifican que no
    // hubo transicion, lectura de registro ni PRINT antes del fallo de carga.
    error << L"LCERO_STATUS=ERROR_DE_CARGA LCERO_STEPS=0 PC=0 CATEGORY=CARGA";
    if (diagnostic.source_line.has_value()) {
        error << L" LINE=" << *diagnostic.source_line;
    }
    error << L" CODE=" << widen_ascii(diagnostic_code_name(diagnostic.code))
          << L" MESSAGE=" << widen_ascii(diagnostic.message) << L'\n';
}

void print_resource_diagnostic(std::wostream& error,
                               const Diagnostic& diagnostic) {
    error << L"LCERO_CLI_ERROR=RECURSO CODE="
          << widen_ascii(diagnostic_code_name(diagnostic.code))
          << L" MESSAGE=" << widen_ascii(diagnostic.message) << L'\n';
}

void print_trace_diagnostic(std::wostream& error,
                            const Diagnostic& diagnostic) {
    error << L"LCERO_CLI_ERROR=TRAZA CODE="
          << widen_ascii(diagnostic_code_name(diagnostic.code))
          << L" MESSAGE=" << widen_ascii(diagnostic.message) << L'\n';
}

void print_execution_status(std::wostream& error,
                            const ExecutionState& state) {
    error << L"LCERO_STATUS="
          << widen_ascii(machine_status_name(state.status()))
          << L" LCERO_STEPS=" << state.executed_steps()
          << L" PC=" << state.program_counter()
          << L" CATEGORY=";
    if (state.status() == MachineStatus::halt) {
        error << L"TERMINACION";
    } else if (state.status() == MachineStatus::limit) {
        error << L"PRESUPUESTO";
    } else if (state.terminal_diagnostic().has_value() &&
               state.terminal_diagnostic()->category ==
                   DiagnosticCategory::resource) {
        error << L"RECURSOS";
    } else {
        error << L"EJECUCION";
    }
    if (state.terminal_diagnostic().has_value()) {
        const Diagnostic& diagnostic = *state.terminal_diagnostic();
        if (diagnostic.source_line.has_value()) {
            error << L" LINE=" << *diagnostic.source_line;
        }
        error << L" CODE=" << widen_ascii(diagnostic_code_name(diagnostic.code))
              << L" MESSAGE=" << widen_ascii(diagnostic.message);
    }
    error << L'\n';
}

void print_execution_resource_error(std::wostream& error,
                                    const ExecutionState& state,
                                    const Diagnostic& diagnostic) {
    // Si falla el canal fisico de PRINT, el proceso no puede declarar el HALT
    // previamente calculado como exito observable. Se conserva pc y k y se usa
    // el estado existente ERROR con categoria RECURSOS.
    error << L"LCERO_STATUS=ERROR LCERO_STEPS=" << state.executed_steps()
          << L" PC=" << state.program_counter()
          << L" CATEGORY=RECURSOS CODE="
          << widen_ascii(diagnostic_code_name(diagnostic.code))
          << L" MESSAGE=" << widen_ascii(diagnostic.message) << L'\n';
}

Result<std::uint64_t> parse_step_limit(const std::wstring& text) {
    // El presupuesto es una convencion de la CLI y cabe en k (uint64_t); no es
    // un entero de la RI ni reemplaza el dominio cpp_int de registros y memoria.
    if (text.empty() || (text.size() > 1U && text.front() == L'0')) {
        return Result<std::uint64_t>::failure(
            {DiagnosticCategory::internal,
             DiagnosticCode::invalid_instruction_state,
             "el limite debe ser 0 o un decimal positivo sin ceros iniciales"});
    }

    std::uint64_t value = 0;
    for (const wchar_t character : text) {
        if (character < L'0' || character > L'9') {
            return Result<std::uint64_t>::failure(
                {DiagnosticCategory::internal,
                 DiagnosticCode::invalid_instruction_state,
                 "el limite de pasos debe contener solamente digitos"});
        }
        const std::uint64_t digit =
            static_cast<std::uint64_t>(character - L'0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return Result<std::uint64_t>::failure(
                {DiagnosticCategory::internal,
                 DiagnosticCode::invalid_instruction_state,
                 "el limite de pasos excede la capacidad del contador k"});
        }
        value = value * 10U + digit;
    }
    return Result<std::uint64_t>::success(value);
}

Result<ParsedCommand> parse_command(
    const std::vector<std::wstring>& arguments) {
    ParsedCommand command;
    bool has_path = false;
    bool has_max_steps = false;
    bool has_no_step_limit = false;
    bool has_trace = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::wstring& argument = arguments[index];
        if (argument == L"--check") {
            if (command.check_mode) {
                return Result<ParsedCommand>::failure(
                    {DiagnosticCategory::internal,
                     DiagnosticCode::invalid_instruction_state,
                     "--check no puede repetirse"});
            }
            command.check_mode = true;
            continue;
        }
        if (argument == L"--no-step-limit") {
            if (has_no_step_limit || has_max_steps) {
                return Result<ParsedCommand>::failure(
                    {DiagnosticCategory::internal,
                     DiagnosticCode::invalid_instruction_state,
                     "--no-step-limit no se combina con otro limite"});
            }
            has_no_step_limit = true;
            command.execution_options.max_steps = std::nullopt;
            continue;
        }
        if (argument == L"--max-steps") {
            if (has_max_steps || has_no_step_limit ||
                index + 1U >= arguments.size()) {
                return Result<ParsedCommand>::failure(
                    {DiagnosticCategory::internal,
                     DiagnosticCode::invalid_instruction_state,
                     "--max-steps requiere un unico valor N"});
            }
            Result<std::uint64_t> parsed =
                parse_step_limit(arguments[++index]);
            if (!parsed.ok()) {
                return Result<ParsedCommand>::failure(*parsed.diagnostic);
            }
            has_max_steps = true;
            command.execution_options.max_steps = *parsed.value;
            continue;
        }
        if (argument == L"--trace") {
            if (has_trace || index + 1U >= arguments.size()) {
                return Result<ParsedCommand>::failure(
                    {DiagnosticCategory::internal,
                     DiagnosticCode::invalid_instruction_state,
                     "--trace requiere una unica ruta de log"});
            }
            const std::wstring& trace_argument = arguments[++index];
            if (trace_argument.empty() || trace_argument.front() == L'-') {
                return Result<ParsedCommand>::failure(
                    {DiagnosticCategory::internal,
                     DiagnosticCode::invalid_instruction_state,
                     "la ruta de --trace no puede estar vacia ni ser otra opcion"});
            }
            has_trace = true;
            command.trace_path = std::filesystem::path(trace_argument);
            continue;
        }
        if (!argument.empty() && argument.front() == L'-') {
            return Result<ParsedCommand>::failure(
                {DiagnosticCategory::internal,
                 DiagnosticCode::invalid_instruction_state,
                 "opcion no reconocida"});
        }
        if (has_path || argument.empty()) {
            return Result<ParsedCommand>::failure(
                {DiagnosticCategory::internal,
                 DiagnosticCode::invalid_instruction_state,
                 "se requiere exactamente una ruta .lci"});
        }
        has_path = true;
        command.input_path = std::filesystem::path(argument);
    }

    if (!has_path) {
        return Result<ParsedCommand>::failure(
            {DiagnosticCategory::internal,
             DiagnosticCode::invalid_instruction_state,
             "falta la ruta de entrada .lci"});
    }
    if (command.check_mode &&
        (has_max_steps || has_no_step_limit || has_trace)) {
        return Result<ParsedCommand>::failure(
            {DiagnosticCategory::internal,
             DiagnosticCode::invalid_instruction_state,
             "--check no ejecuta y no admite presupuesto ni traza"});
    }
    return Result<ParsedCommand>::success(std::move(command));
}

Result<ValidatedIr> load_and_validate(
    const std::filesystem::path& input_path) {
    Result<IrSource> loaded = load_ir_source(input_path);
    if (!loaded.ok()) {
        return Result<ValidatedIr>::failure(*loaded.diagnostic);
    }
    return validate_ir(*loaded.value);
}

int report_load_failure(std::wostream& standard_error,
                        const Diagnostic& diagnostic) {
    if (is_physical_input_failure(diagnostic)) {
        print_resource_diagnostic(standard_error, diagnostic);
        return as_integer(ExitCode::usage_or_file_error);
    }
    print_load_diagnostic(standard_error, diagnostic);
    return as_integer(ExitCode::load_error);
}

int run_check(const std::filesystem::path& input_path,
              std::wostream& standard_error) {
    const Result<ValidatedIr> validated = load_and_validate(input_path);
    if (!validated.ok()) {
        return report_load_failure(standard_error, *validated.diagnostic);
    }

    // Este mensaje certifica solamente propiedades estaticas de la RI. No
    // afirma alcanzabilidad de HALT ni comportamiento de sus transiciones.
    standard_error << L"LCERO_CHECK=VALID\n";
    return as_integer(ExitCode::success);
}

int run_program(const std::filesystem::path& input_path,
                const ExecutionOptions& options,
                const std::optional<std::filesystem::path>& trace_path,
                std::ostream& standard_output,
                std::wostream& standard_error) {
    Result<ValidatedIr> validated = load_and_validate(input_path);
    if (!validated.ok()) {
        return report_load_failure(standard_error, *validated.diagnostic);
    }

    std::optional<TraceWriter> trace_writer;
    ExecutionOptions effective_options = options;
    if (trace_path.has_value()) {
        Result<TraceWriter> opened = TraceWriter::open(*trace_path);
        if (!opened.ok()) {
            print_trace_diagnostic(standard_error, *opened.diagnostic);
            return as_integer(ExitCode::usage_or_file_error);
        }
        trace_writer.emplace(std::move(*opened.value));
        trace_writer->write_header(*validated.value);
        if (!trace_writer->ok()) {
            print_trace_diagnostic(standard_error,
                                   *trace_writer->diagnostic());
            return as_integer(ExitCode::usage_or_file_error);
        }
        effective_options.trace_writer = &*trace_writer;
    }

    Result<ExecutionState> execution =
        interpret(std::move(*validated.value), effective_options);
    if (!execution.ok()) {
        print_resource_diagnostic(standard_error, *execution.diagnostic);
        return as_integer(ExitCode::usage_or_file_error);
    }

    const ExecutionState& state = *execution.value;
    if (trace_writer.has_value()) {
        trace_writer->finalize(state);
    }
    // output_text materializa O con un byte LF por PRINT. main configuro stdout
    // binario para impedir que Windows lo traduzca a CRLF al redirigirlo.
    std::string output;
    try {
        output = state.output_text();
        standard_output.write(output.data(),
                              static_cast<std::streamsize>(output.size()));
        standard_output.flush();
    } catch (const std::bad_alloc&) {
        print_execution_resource_error(
            standard_error, state,
            {DiagnosticCategory::resource,
             DiagnosticCode::resource_exhausted,
             "memoria insuficiente para serializar PRINT"});
        return as_integer(ExitCode::execution_error);
    } catch (const std::ios_base::failure&) {
        print_execution_resource_error(
            standard_error, state,
            {DiagnosticCategory::resource,
             DiagnosticCode::output_write_failed,
             "el canal de salida rechazo los bytes PRINT"});
        return as_integer(ExitCode::execution_error);
    }
    if (!standard_output.good()) {
        print_execution_resource_error(
            standard_error, state,
            {DiagnosticCategory::resource,
             DiagnosticCode::output_write_failed,
             "no se pudo escribir completamente la salida PRINT"});
        return as_integer(ExitCode::execution_error);
    }

    if (trace_writer.has_value() && !trace_writer->ok()) {
        print_execution_resource_error(standard_error, state,
                                       *trace_writer->diagnostic());
        return as_integer(ExitCode::execution_error);
    }

    print_execution_status(standard_error, state);
    switch (state.status()) {
        case MachineStatus::halt:
            return as_integer(ExitCode::success);
        case MachineStatus::error:
            return as_integer(ExitCode::execution_error);
        case MachineStatus::limit:
            return as_integer(ExitCode::step_limit);
        case MachineStatus::running:
        case MachineStatus::load_error:
            print_cli_error(standard_error, L"INTERNO",
                            L"estado terminal inesperado del interprete");
            return as_integer(ExitCode::usage_or_file_error);
    }
    return as_integer(ExitCode::usage_or_file_error);
}

}  // namespace

int run(const std::vector<std::wstring>& arguments,
        std::ostream& standard_output,
        std::wostream& standard_error,
        const std::filesystem::path& executable_path) {
    if (arguments.size() == 1U && arguments.front() == L"--help") {
        print_help(standard_output);
        return as_integer(ExitCode::success);
    }
    if (arguments.size() == 1U && arguments.front() == L"--version") {
        standard_output << kVersion << '\n';
        return as_integer(ExitCode::success);
    }
    ParsedCommand command;
    if (arguments.empty()) {
        command.input_path = default_input_path(executable_path);
    } else {
        Result<ParsedCommand> parsed = parse_command(arguments);
        if (!parsed.ok()) {
            print_cli_error(standard_error, L"ARGUMENTOS",
                            widen_ascii(parsed.diagnostic->message) + L"; use --help");
            return as_integer(ExitCode::usage_or_file_error);
        }
        command = std::move(*parsed.value);
    }
    if (!has_lci_extension(command.input_path)) {
        print_cli_error(standard_error, L"ARCHIVO",
                        L"la VM acepta exclusivamente archivos .lci");
        return as_integer(ExitCode::usage_or_file_error);
    }
    if (command.trace_path.has_value() &&
        has_lci_extension(*command.trace_path)) {
        print_cli_error(standard_error, L"TRAZA",
                        L"el log no puede usar la extension reservada .lci");
        return as_integer(ExitCode::usage_or_file_error);
    }

    const std::filesystem::path input_path = arguments.empty()
        ? command.input_path
        : resolve_input_path(command.input_path, executable_path);

    std::error_code file_error;
    const bool is_regular_input =
        std::filesystem::is_regular_file(input_path, file_error);
    if (file_error || !is_regular_input) {
        print_cli_error(standard_error, L"ARCHIVO",
                        L"entrada inexistente, inaccesible o no regular");
        return as_integer(ExitCode::usage_or_file_error);
    }

    if (command.check_mode) {
        return run_check(input_path, standard_error);
    }
    return run_program(input_path, command.execution_options,
                       command.trace_path,
                       standard_output, standard_error);
}

}  // namespace lcero::vm::cli
