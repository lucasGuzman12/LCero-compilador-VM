#pragma once

#include "lcero/common/diagnostic.hpp"
#include "lcero/ir/ir.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <set>
#include <vector>

namespace lcero::ir {

using TemporarySet = std::set<std::size_t>;

struct BasicBlock {
    std::size_t id = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::vector<std::size_t> successors;
    std::vector<std::size_t> predecessors;
    TemporarySet use;
    TemporarySet definition;
    TemporarySet live_in;
    TemporarySet live_out;
};

struct ControlFlowGraph {
    std::vector<BasicBlock> blocks;
    std::vector<std::size_t> instruction_to_block;
    std::size_t edge_count = 0;
};

struct CfgResult {
    bool succeeded = false;
    ControlFlowGraph graph;
};

class ControlFlowGraphBuilder {
public:
    [[nodiscard]] CfgResult build(
        const Program& program,
        const std::filesystem::path& source_path,
        common::DiagnosticBag& diagnostics) const;
};

struct LivenessResult {
    bool succeeded = false;
    ControlFlowGraph graph;
    std::vector<TemporarySet> instruction_use;
    std::vector<TemporarySet> instruction_definition;
    std::vector<TemporarySet> instruction_live_in;
    std::vector<TemporarySet> instruction_live_out;
    std::size_t fixed_point_iterations = 0;
};

class LivenessAnalyzer {
public:
    [[nodiscard]] LivenessResult analyze(
        const Program& program,
        ControlFlowGraph graph,
        const std::filesystem::path& source_path,
        common::DiagnosticBag& diagnostics) const;
};

struct InterferenceGraph {
    std::vector<TemporarySet> adjacency;
    std::vector<std::size_t> use_frequency;
    std::size_t edge_count = 0;
};

class InterferenceGraphBuilder {
public:
    [[nodiscard]] InterferenceGraph build(
        const Program& program,
        const LivenessResult& liveness) const;
};

struct RegisterAllocation {
    bool succeeded = false;
    std::size_t assignable_register_count = 0;
    std::vector<std::optional<std::size_t>> colors;
    std::vector<std::optional<std::size_t>> spill_slots;
    std::vector<std::size_t> simplify_order;
    std::vector<std::size_t> potential_spill_order;
    std::size_t registers_used = 0;
    std::size_t spilled_temporary_count = 0;
};

class ChaitinAllocator {
public:
    [[nodiscard]] RegisterAllocation allocate(
        const InterferenceGraph& graph,
        std::size_t variable_count,
        std::size_t assignable_register_count = 2) const;
};

[[nodiscard]] TemporarySet instruction_uses(const Instruction& instruction);
[[nodiscard]] TemporarySet instruction_definitions(const Instruction& instruction);

} // namespace lcero::ir
