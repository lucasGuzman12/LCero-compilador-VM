#pragma once

#include "lcero/ast/ast.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <string>

namespace lcero::semantic {

enum class SymbolType {
    Integer
};

struct Symbol {
    std::string name;
    SymbolType type = SymbolType::Integer;
    ast::SourcePosition declaration_position;
};

struct SymbolInsertionResult {
    bool inserted = false;
    const Symbol* symbol = nullptr;
};

// D se representa mediante las claves de este mapa. L-Cero tiene un unico
// ambito global, por lo que no existe una pila de tablas ni ocultamiento.
class SymbolTable {
public:
    [[nodiscard]] SymbolInsertionResult insert(const ast::Declaration& declaration);
    [[nodiscard]] const Symbol* find(const std::string& name) const noexcept;
    [[nodiscard]] bool contains(const std::string& name) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::set<std::string> declared_names() const;
    [[nodiscard]] const std::map<std::string, Symbol>& symbols() const noexcept;

private:
    std::map<std::string, Symbol> symbols_;
};

[[nodiscard]] const char* symbol_type_name(SymbolType type) noexcept;

} // namespace lcero::semantic
