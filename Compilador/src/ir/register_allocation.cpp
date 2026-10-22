#include "lcero/ir/register_allocation.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <type_traits>

namespace lcero::ir {
namespace {

void report_analysis_error(
    common::DiagnosticBag& diagnostics,
    const std::filesystem::path& path,
    const std::string& message) {
    diagnostics.report(
        common::DiagnosticCategory::CodeGeneration,
        common::DiagnosticSeverity::Error,
        message,
        common::SourceLocation{path});
}

TemporarySet set_union_of(const TemporarySet& left, const TemporarySet& right) {
    TemporarySet result;
    std::set_union(left.begin(), left.end(), right.begin(), right.end(),
                   std::inserter(result, result.end()));
    return result;
}

TemporarySet set_difference_of(
    const TemporarySet& left,
    const TemporarySet& right) {
    TemporarySet result;
    std::set_difference(left.begin(), left.end(), right.begin(), right.end(),
                        std::inserter(result, result.end()));
    return result;
}

void add_edge(InterferenceGraph& graph, const std::size_t a, const std::size_t b) {
    if (a == b) {
        return;
    }
    graph.adjacency[a].insert(b);
    graph.adjacency[b].insert(a);
}

void add_clique(InterferenceGraph& graph, const TemporarySet& values) {
    for (auto left = values.begin(); left != values.end(); ++left) {
        for (auto right = std::next(left); right != values.end(); ++right) {
            add_edge(graph, *left, *right);
        }
    }
}

} // namespace

TemporarySet instruction_uses(const Instruction& instruction) {
    return std::visit([](const auto& value) -> TemporarySet {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, StoreVariableInstruction>) {
            return {value.source.id};
        } else if constexpr (std::is_same_v<T, BinaryInstruction> ||
                             std::is_same_v<T, ComparisonInstruction>) {
            return {value.left.id, value.right.id};
        } else if constexpr (std::is_same_v<T, JumpIfZeroInstruction>) {
            return {value.condition.id};
        } else if constexpr (std::is_same_v<T, PrintInstruction>) {
            return {value.source.id};
        } else {
            return {};
        }
    }, instruction);
}

TemporarySet instruction_definitions(const Instruction& instruction) {
    return std::visit([](const auto& value) -> TemporarySet {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ConstantInstruction> ||
                      std::is_same_v<T, LoadVariableInstruction> ||
                      std::is_same_v<T, BinaryInstruction> ||
                      std::is_same_v<T, ComparisonInstruction>) {
            return {value.destination.id};
        } else {
            return {};
        }
    }, instruction);
}

CfgResult ControlFlowGraphBuilder::build(
    const Program& program,
    const std::filesystem::path& source_path,
    common::DiagnosticBag& diagnostics) const {
    CfgResult result;
    const auto instruction_count = program.instructions.size();
    if (instruction_count == 0) {
        report_analysis_error(diagnostics, source_path,
                              "No se puede construir un CFG para una RI vacia.");
        return result;
    }

    std::map<std::size_t, std::size_t> labels;
    for (std::size_t index = 0; index < instruction_count; ++index) {
        if (const auto* label = std::get_if<LabelInstruction>(
                &program.instructions[index].instruction)) {
            if (label->label.id >= program.label_count ||
                !labels.emplace(label->label.id, index).second) {
                report_analysis_error(diagnostics, source_path,
                                      "La RI contiene una etiqueta invalida o duplicada.");
                return result;
            }
        }
    }

    std::set<std::size_t> leaders{0};
    for (const auto& item : labels) {
        leaders.insert(item.second);
    }
    for (std::size_t index = 0; index < instruction_count; ++index) {
        const auto& instruction = program.instructions[index].instruction;
        std::optional<std::size_t> target;
        if (const auto* jump = std::get_if<JumpInstruction>(&instruction)) {
            target = jump->target.id;
        } else if (const auto* jump_zero =
                       std::get_if<JumpIfZeroInstruction>(&instruction)) {
            target = jump_zero->target.id;
        }
        if (target.has_value()) {
            const auto found = labels.find(target.value());
            if (found == labels.end()) {
                report_analysis_error(diagnostics, source_path,
                                      "El CFG encontro un salto a una etiqueta inexistente.");
                return result;
            }
            leaders.insert(found->second);
        }
        if ((target.has_value() || std::holds_alternative<HaltInstruction>(instruction)) &&
            index + 1 < instruction_count) {
            leaders.insert(index + 1);
        }
    }

    const std::vector<std::size_t> ordered_leaders(leaders.begin(), leaders.end());
    result.graph.instruction_to_block.resize(instruction_count);
    for (std::size_t index = 0; index < ordered_leaders.size(); ++index) {
        const auto begin = ordered_leaders[index];
        const auto end = index + 1 < ordered_leaders.size()
            ? ordered_leaders[index + 1]
            : instruction_count;
        const auto block_id = result.graph.blocks.size();
        result.graph.blocks.push_back(BasicBlock{block_id, begin, end});
        for (std::size_t instruction = begin; instruction < end; ++instruction) {
            result.graph.instruction_to_block[instruction] = block_id;
        }
    }

    auto target_block = [&](const LabelId label) -> std::optional<std::size_t> {
        const auto found = labels.find(label.id);
        if (found == labels.end()) {
            return std::nullopt;
        }
        return result.graph.instruction_to_block[found->second];
    };

    for (auto& block : result.graph.blocks) {
        const auto& last = program.instructions[block.end - 1].instruction;
        std::set<std::size_t> successors;
        if (const auto* jump = std::get_if<JumpInstruction>(&last)) {
            successors.insert(target_block(jump->target).value());
        } else if (const auto* jump_zero = std::get_if<JumpIfZeroInstruction>(&last)) {
            successors.insert(target_block(jump_zero->target).value());
            if (block.id + 1 < result.graph.blocks.size()) {
                successors.insert(block.id + 1);
            }
        } else if (!std::holds_alternative<HaltInstruction>(last) &&
                   block.id + 1 < result.graph.blocks.size()) {
            successors.insert(block.id + 1);
        }
        block.successors.assign(successors.begin(), successors.end());
        result.graph.edge_count += block.successors.size();
    }
    for (const auto& block : result.graph.blocks) {
        for (const auto successor : block.successors) {
            result.graph.blocks[successor].predecessors.push_back(block.id);
        }
    }

    result.succeeded = true;
    return result;
}

LivenessResult LivenessAnalyzer::analyze(
    const Program& program,
    ControlFlowGraph graph,
    const std::filesystem::path& source_path,
    common::DiagnosticBag& diagnostics) const {
    LivenessResult result;
    const auto count = program.instructions.size();
    result.instruction_use.resize(count);
    result.instruction_definition.resize(count);
    result.instruction_live_in.resize(count);
    result.instruction_live_out.resize(count);

    for (std::size_t index = 0; index < count; ++index) {
        result.instruction_use[index] =
            instruction_uses(program.instructions[index].instruction);
        result.instruction_definition[index] =
            instruction_definitions(program.instructions[index].instruction);
        for (const auto temporary : set_union_of(
                 result.instruction_use[index],
                 result.instruction_definition[index])) {
            if (temporary >= program.temporary_count) {
                report_analysis_error(diagnostics, source_path,
                                      "La vivacidad encontro un temporal fuera de rango.");
                return result;
            }
        }
    }

    for (auto& block : graph.blocks) {
        for (std::size_t index = block.begin; index < block.end; ++index) {
            for (const auto used : result.instruction_use[index]) {
                if (block.definition.count(used) == 0) {
                    block.use.insert(used);
                }
            }
            block.definition.insert(result.instruction_definition[index].begin(),
                                    result.instruction_definition[index].end());
        }
    }

    // Las ecuaciones monotónicas sobre el retículo finito de subconjuntos de
    // Temp convergen. El recorrido inverso solo acelera la llegada al punto fijo.
    bool changed = false;
    do {
        changed = false;
        ++result.fixed_point_iterations;
        for (auto item = graph.blocks.rbegin(); item != graph.blocks.rend(); ++item) {
            TemporarySet new_out;
            for (const auto successor : item->successors) {
                new_out = set_union_of(new_out, graph.blocks[successor].live_in);
            }
            const auto new_in = set_union_of(
                item->use,
                set_difference_of(new_out, item->definition));
            if (new_in != item->live_in || new_out != item->live_out) {
                item->live_in = new_in;
                item->live_out = new_out;
                changed = true;
            }
        }
    } while (changed);

    for (const auto& block : graph.blocks) {
        auto live = block.live_out;
        for (std::size_t index = block.end; index-- > block.begin;) {
            result.instruction_live_out[index] = live;
            live = set_union_of(
                result.instruction_use[index],
                set_difference_of(live, result.instruction_definition[index]));
            result.instruction_live_in[index] = live;
        }
        if (live != block.live_in) {
            report_analysis_error(diagnostics, source_path,
                                  "La vivacidad por instruccion no coincide con el bloque.");
            return result;
        }
    }

    result.graph = std::move(graph);
    result.succeeded = true;
    return result;
}

InterferenceGraph InterferenceGraphBuilder::build(
    const Program& program,
    const LivenessResult& liveness) const {
    InterferenceGraph graph;
    graph.adjacency.resize(program.temporary_count);
    graph.use_frequency.resize(program.temporary_count);

    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        const auto& uses = liveness.instruction_use[index];
        const auto& definitions = liveness.instruction_definition[index];
        for (const auto used : uses) {
            ++graph.use_frequency[used];
        }

        // Dos valores vivos en el mismo punto no pueden compartir color. Las
        // fuentes simultáneas también interfieren aunque ambas mueran aquí.
        add_clique(graph, liveness.instruction_live_in[index]);
        add_clique(graph, liveness.instruction_live_out[index]);
        add_clique(graph, uses);
        for (const auto defined : definitions) {
            for (const auto live : liveness.instruction_live_out[index]) {
                add_edge(graph, defined, live);
            }
        }
    }

    std::size_t doubled_edges = 0;
    for (const auto& neighbors : graph.adjacency) {
        doubled_edges += neighbors.size();
    }
    graph.edge_count = doubled_edges / 2;
    return graph;
}

RegisterAllocation ChaitinAllocator::allocate(
    const InterferenceGraph& graph,
    const std::size_t variable_count,
    const std::size_t assignable_register_count) const {
    RegisterAllocation result;
    result.assignable_register_count = assignable_register_count;
    result.colors.resize(graph.adjacency.size());
    result.spill_slots.resize(graph.adjacency.size());
    if (assignable_register_count == 0 ||
        graph.use_frequency.size() != graph.adjacency.size()) {
        return result;
    }

    std::vector<TemporarySet> remaining = graph.adjacency;
    std::set<std::size_t> active;
    for (std::size_t node = 0; node < remaining.size(); ++node) {
        active.insert(node);
    }

    while (!active.empty()) {
        std::optional<std::size_t> selected;
        for (const auto node : active) {
            if (remaining[node].size() < assignable_register_count) {
                selected = node;
                break;
            }
        }

        if (!selected.has_value()) {
            // Menor frecuencia/grado se considera menos costoso de derramar.
            // Los productos cruzados evitan decisiones no deterministas por float.
            for (const auto node : active) {
                if (!selected.has_value()) {
                    selected = node;
                    continue;
                }
                const auto current = selected.value();
                const auto node_degree = std::max<std::size_t>(1, remaining[node].size());
                const auto current_degree =
                    std::max<std::size_t>(1, remaining[current].size());
                const auto left = graph.use_frequency[node] * current_degree;
                const auto right = graph.use_frequency[current] * node_degree;
                if (left < right || (left == right && node < current)) {
                    selected = node;
                }
            }
            result.potential_spill_order.push_back(selected.value());
        }

        const auto node = selected.value();
        result.simplify_order.push_back(node);
        active.erase(node);
        for (const auto neighbor : remaining[node]) {
            remaining[neighbor].erase(node);
        }
        remaining[node].clear();
    }

    for (auto item = result.simplify_order.rbegin();
         item != result.simplify_order.rend(); ++item) {
        const auto node = *item;
        std::set<std::size_t> unavailable;
        for (const auto neighbor : graph.adjacency[node]) {
            if (result.colors[neighbor].has_value()) {
                unavailable.insert(result.colors[neighbor].value());
            }
        }
        for (std::size_t color = 0; color < assignable_register_count; ++color) {
            if (unavailable.count(color) == 0) {
                result.colors[node] = color;
                break;
            }
        }
    }

    std::size_t next_slot = variable_count;
    std::set<std::size_t> used_colors;
    for (std::size_t node = 0; node < result.colors.size(); ++node) {
        if (result.colors[node].has_value()) {
            used_colors.insert(result.colors[node].value());
        } else {
            result.spill_slots[node] = next_slot++;
            ++result.spilled_temporary_count;
        }
    }
    result.registers_used = used_colors.size();

    for (std::size_t node = 0; node < graph.adjacency.size(); ++node) {
        for (const auto neighbor : graph.adjacency[node]) {
            if (result.colors[node].has_value() && result.colors[neighbor].has_value() &&
                result.colors[node] == result.colors[neighbor]) {
                return result;
            }
        }
    }
    result.succeeded = true;
    return result;
}

} // namespace lcero::ir
