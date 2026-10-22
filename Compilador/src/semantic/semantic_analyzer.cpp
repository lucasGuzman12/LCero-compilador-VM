#include "lcero/semantic/semantic_analyzer.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace lcero::semantic {

SemanticAnalysisResult SemanticAnalyzer::analyze(
    const ast::Program& program,
    const std::filesystem::path& source_path,
    common::DiagnosticBag& diagnostics) {
    symbol_table_ = SymbolTable{};
    transfers_.clear();
    source_path_ = source_path;
    diagnostics_ = &diagnostics;
    error_reported_ = false;

    InitializationSet initialized;
    if (!declare_all(program.declarations) ||
        !validate_invariant(initialized, program.position) ||
        !analyze_statements(program.statements, initialized)) {
        return SemanticAnalysisResult{
            false,
            std::move(symbol_table_),
            std::move(initialized),
            std::move(transfers_)
        };
    }

    return SemanticAnalysisResult{
        true,
        std::move(symbol_table_),
        std::move(initialized),
        std::move(transfers_)
    };
}

bool SemanticAnalyzer::declare_all(
    const std::vector<ast::Declaration>& declarations) {
    for (const auto& declaration : declarations) {
        const auto insertion = symbol_table_.insert(declaration);
        if (!insertion.inserted) {
            report_duplicate(declaration, *insertion.symbol);
            return false;
        }
    }
    return true;
}

bool SemanticAnalyzer::analyze_statements(
    const std::vector<std::unique_ptr<ast::Statement>>& statements,
    InitializationSet& initialized) {
    for (const auto& statement : statements) {
        if (!validate_invariant(initialized, statement->position) ||
            !analyze_statement(*statement, initialized) ||
            !validate_invariant(initialized, statement->position)) {
            return false;
        }
    }
    return true;
}

bool SemanticAnalyzer::analyze_statement(
    const ast::Statement& statement,
    InitializationSet& initialized) {
    if (const auto* assignment =
            dynamic_cast<const ast::AssignmentStatement*>(&statement)) {
        const auto input = initialized;

        // La expresion se valida con I_entrada. Recién despues se incorpora el
        // destino, evitando aceptar x = x + 1 como primera asignacion de x.
        if (!validate_expression(*assignment->expression, input)) {
            return false;
        }
        if (!symbol_table_.contains(assignment->name)) {
            report_undeclared(assignment->name, assignment->position);
            return false;
        }
        initialized.insert(assignment->name);
        transfers_.push_back(TransferRecord{
            TransferKind::Assignment,
            assignment->position,
            input,
            initialized,
            {},
            {}
        });
        return true;
    }

    if (const auto* print = dynamic_cast<const ast::PrintStatement*>(&statement)) {
        const auto input = initialized;
        if (!validate_expression(*print->expression, input)) {
            return false;
        }
        transfers_.push_back(TransferRecord{
            TransferKind::Print,
            print->position,
            input,
            initialized,
            {},
            {}
        });
        return true;
    }

    if (const auto* branch = dynamic_cast<const ast::IfStatement*>(&statement)) {
        const auto input = initialized;
        if (!validate_condition(*branch->condition, input)) {
            return false;
        }

        InitializationSet then_state = input;
        if (!analyze_block(*branch->then_block, then_state)) {
            return false;
        }

        if (branch->else_block != nullptr) {
            InitializationSet else_state = input;
            if (!analyze_block(*branch->else_block, else_state)) {
                return false;
            }

            // Inicializacion definitiva: solo sobrevive lo garantizado por las
            // dos ramas posibles, I_salida = I_then interseccion I_else.
            InitializationSet intersection;
            std::set_intersection(
                then_state.begin(), then_state.end(),
                else_state.begin(), else_state.end(),
                std::inserter(intersection, intersection.end()));
            initialized = intersection;
            transfers_.push_back(TransferRecord{
                TransferKind::IfWithElse,
                branch->position,
                input,
                initialized,
                then_state,
                else_state
            });
        } else {
            // La rama implicita puede no ejecutar el bloque. Como las
            // transferencias nunca desinicializan, la interseccion con la
            // entrada equivale exactamente a conservar I_entrada.
            initialized = input;
            transfers_.push_back(TransferRecord{
                TransferKind::IfWithoutElse,
                branch->position,
                input,
                initialized,
                then_state,
                input
            });
        }
        return true;
    }

    if (const auto* loop = dynamic_cast<const ast::WhileStatement*>(&statement)) {
        const auto input = initialized;
        if (!validate_condition(*loop->condition, input)) {
            return false;
        }

        InitializationSet body_state = input;
        if (!analyze_block(*loop->body, body_state)) {
            return false;
        }

        // El cuerpo puede ejecutarse cero veces: se valida por completo, pero
        // ninguna inicializacion obtenida solo dentro del while se propaga.
        initialized = input;
        transfers_.push_back(TransferRecord{
            TransferKind::While,
            loop->position,
            input,
            initialized,
            body_state,
            input
        });
        return true;
    }

    report_internal("Tipo de instruccion AST desconocido.", statement.position);
    return false;
}

bool SemanticAnalyzer::analyze_block(
    const ast::Block& block,
    InitializationSet& initialized) {
    return analyze_statements(block.statements, initialized);
}

bool SemanticAnalyzer::validate_condition(
    const ast::Condition& condition,
    const InitializationSet& initialized) {
    return validate_expression(*condition.left, initialized) &&
        validate_expression(*condition.right, initialized);
}

bool SemanticAnalyzer::validate_expression(
    const ast::Expression& expression,
    const InitializationSet& initialized) {
    if (dynamic_cast<const ast::IntegerLiteral*>(&expression) != nullptr) {
        return true;
    }

    if (const auto* variable =
            dynamic_cast<const ast::VariableExpression*>(&expression)) {
        if (!symbol_table_.contains(variable->name)) {
            report_undeclared(variable->name, variable->position);
            return false;
        }
        if (initialized.count(variable->name) == 0) {
            report_uninitialized(variable->name, variable->position);
            return false;
        }
        return true;
    }

    if (const auto* unary = dynamic_cast<const ast::UnaryExpression*>(&expression)) {
        return validate_expression(*unary->operand, initialized);
    }

    if (const auto* binary = dynamic_cast<const ast::BinaryExpression*>(&expression)) {
        // No se evalua el divisor: la division por cero pertenece a ejecucion.
        return validate_expression(*binary->left, initialized) &&
            validate_expression(*binary->right, initialized);
    }

    report_internal("Tipo de expresion AST desconocido.", expression.position);
    return false;
}

bool SemanticAnalyzer::validate_invariant(
    const InitializationSet& initialized,
    const ast::SourcePosition& position) {
    // Invariante central I subconjunto D. Se comprueba en cada frontera de
    // transferencia; una violacion indicaria un defecto interno, no del fuente.
    for (const auto& name : initialized) {
        if (!symbol_table_.contains(name)) {
            report_internal(
                "El estado de inicializacion contiene una variable no declarada: '" +
                    name + "'.",
                position);
            return false;
        }
    }
    return true;
}

void SemanticAnalyzer::report_duplicate(
    const ast::Declaration& declaration,
    const Symbol& previous) {
    if (error_reported_) {
        return;
    }
    error_reported_ = true;
    diagnostics_->report(
        common::DiagnosticCategory::Semantic,
        common::DiagnosticSeverity::Error,
        "Declaracion duplicada de '" + declaration.name +
            "'; la declaracion anterior esta en linea " +
            std::to_string(previous.declaration_position.line) +
            ", columna " +
            std::to_string(previous.declaration_position.column) + ".",
        location(declaration.position));
}

void SemanticAnalyzer::report_undeclared(
    const std::string& name,
    const ast::SourcePosition& position) {
    if (error_reported_) {
        return;
    }
    error_reported_ = true;
    diagnostics_->report(
        common::DiagnosticCategory::Semantic,
        common::DiagnosticSeverity::Error,
        "Variable no declarada: '" + name + "'.",
        location(position));
}

void SemanticAnalyzer::report_uninitialized(
    const std::string& name,
    const ast::SourcePosition& position) {
    if (error_reported_) {
        return;
    }
    error_reported_ = true;
    diagnostics_->report(
        common::DiagnosticCategory::Semantic,
        common::DiagnosticSeverity::Error,
        "Variable no inicializada: '" + name + "'.",
        location(position));
}

void SemanticAnalyzer::report_internal(
    const std::string& message,
    const ast::SourcePosition& position) {
    if (error_reported_) {
        return;
    }
    error_reported_ = true;
    diagnostics_->report(
        common::DiagnosticCategory::Internal,
        common::DiagnosticSeverity::Error,
        message,
        location(position));
}

common::SourceLocation SemanticAnalyzer::location(
    const ast::SourcePosition& position) const {
    return common::SourceLocation{source_path_, position.line, position.column};
}

const char* transfer_kind_name(const TransferKind kind) noexcept {
    switch (kind) {
    case TransferKind::Assignment: return "asignacion";
    case TransferKind::Print: return "print";
    case TransferKind::IfWithElse: return "if_else";
    case TransferKind::IfWithoutElse: return "if_sin_else";
    case TransferKind::While: return "while";
    }
    return "desconocida";
}

} // namespace lcero::semantic
