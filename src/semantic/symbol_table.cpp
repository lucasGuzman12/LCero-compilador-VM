#include "lcero/semantic/symbol_table.hpp"

namespace lcero::semantic {

SymbolInsertionResult SymbolTable::insert(const ast::Declaration& declaration) {
    const auto insertion = symbols_.emplace(
        declaration.name,
        Symbol{declaration.name, SymbolType::Integer, declaration.position});
    return SymbolInsertionResult{insertion.second, &insertion.first->second};
}

const Symbol* SymbolTable::find(const std::string& name) const noexcept {
    const auto item = symbols_.find(name);
    return item == symbols_.end() ? nullptr : &item->second;
}

bool SymbolTable::contains(const std::string& name) const noexcept {
    return find(name) != nullptr;
}

std::size_t SymbolTable::size() const noexcept {
    return symbols_.size();
}

std::set<std::string> SymbolTable::declared_names() const {
    std::set<std::string> result;
    for (const auto& item : symbols_) {
        result.insert(item.first);
    }
    return result;
}

const std::map<std::string, Symbol>& SymbolTable::symbols() const noexcept {
    return symbols_;
}

const char* symbol_type_name(const SymbolType type) noexcept {
    switch (type) {
    case SymbolType::Integer:
        return "entero";
    }
    return "desconocido";
}

} // namespace lcero::semantic
