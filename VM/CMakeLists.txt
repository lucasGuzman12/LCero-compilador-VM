cmake_minimum_required(VERSION 4.2)

project(LCeroVM VERSION 0.7.0 LANGUAGES CXX)

# Boost.Multiprecision proporciona el dominio entero matematico de la maquina.
# La VM interpreta solamente la RI tipada; no incorpora un parser de fuente .lc.
find_package(Boost CONFIG REQUIRED COMPONENTS multiprecision)

add_library(lcero_vm_core STATIC
    src/vm/cli.cpp
    src/vm/diagnostics.cpp
    src/vm/execution_state.cpp
    src/vm/instruction.cpp
    src/vm/interpreter.cpp
    src/vm/ir_loader.cpp
    src/vm/ir_validator.cpp
    src/vm/trace.cpp
)

target_include_directories(lcero_vm_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Boost.Multiprecision aporta cpp_int, el dominio de enteros sin limite fijo
# exigido por LCERO_IR 1. Vincular su target de cabeceras propaga correctamente
# sus includes sin codificar rutas locales ni introducir una DLL.
target_link_libraries(lcero_vm_core PUBLIC Boost::multiprecision)
target_compile_features(lcero_vm_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(lcero_vm_core PRIVATE /W4 /permissive- /utf-8)
endif()

add_executable(lcero_vm src/vm/main.cpp)
target_link_libraries(lcero_vm PRIVATE lcero_vm_core)
set_target_properties(lcero_vm PROPERTIES OUTPUT_NAME "lcero-vm")

if(MSVC)
    target_compile_options(lcero_vm PRIVATE /W4 /permissive- /utf-8)
endif()

add_executable(lcero_vm_tests tests/vm/cli_tests.cpp)
target_link_libraries(lcero_vm_tests PRIVATE lcero_vm_core)

add_executable(lcero_vm_model_tests tests/vm/model_tests.cpp)
target_link_libraries(lcero_vm_model_tests PRIVATE lcero_vm_core)

add_executable(lcero_vm_loader_tests tests/vm/loader_tests.cpp)
target_link_libraries(lcero_vm_loader_tests PRIVATE lcero_vm_core)

add_executable(lcero_vm_interpreter_tests tests/vm/interpreter_tests.cpp)
target_link_libraries(lcero_vm_interpreter_tests PRIVATE lcero_vm_core)

add_executable(lcero_vm_trace_tests tests/vm/trace_tests.cpp)
target_link_libraries(lcero_vm_trace_tests PRIVATE lcero_vm_core)

if(MSVC)
    target_compile_options(lcero_vm_tests PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(lcero_vm_model_tests PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(lcero_vm_loader_tests PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(lcero_vm_interpreter_tests PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(lcero_vm_trace_tests PRIVATE /W4 /permissive- /utf-8)
endif()

enable_testing()
find_program(POWERSHELL_EXECUTABLE NAMES powershell.exe powershell REQUIRED)
add_test(NAME lcero_vm_cli COMMAND lcero_vm_tests)
add_test(NAME lcero_vm_model COMMAND lcero_vm_model_tests)
add_test(NAME lcero_vm_loader COMMAND lcero_vm_loader_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/test/contracts")
add_test(NAME lcero_vm_interpreter COMMAND lcero_vm_interpreter_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/test/contracts")
add_test(NAME lcero_vm_trace COMMAND lcero_vm_trace_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/test/contracts")
add_test(NAME lcero_vm_contract_cli
    COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-contracts.ps1"
        -Executable "$<TARGET_FILE:lcero_vm>"
        -ProjectRoot "${CMAKE_CURRENT_SOURCE_DIR}"
        -Configuration "$<CONFIG>")

# El timeout CTest es una segunda defensa externa. Cada proceso hijo posee
# ademas su propio limite en PowerShell, separado de --max-steps de la VM.
set_tests_properties(lcero_vm_contract_cli PROPERTIES
    TIMEOUT 120
    LABELS "contract;regression"
)

# Los generadores multiconfiguracion de Visual Studio agregan Debug o Release
# debajo de build/bin, dejando una ruta predecible para la CLI de la VM.
set_target_properties(lcero_vm lcero_vm_tests lcero_vm_model_tests lcero_vm_loader_tests lcero_vm_interpreter_tests lcero_vm_trace_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
