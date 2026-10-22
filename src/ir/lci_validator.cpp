#include "lcero/ir/lci_validator.hpp"

#include <charconv>
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lcero::ir {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> tokenize(const std::string& line, bool& valid) {
    std::vector<std::string> tokens;
    std::string current;
    valid = true;
    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (const unsigned char character : line) {
        if (character > 127) {
            valid = false;
            return {};
        }
        if (character == ' ' || character == '\t' || character == '\r') {
            flush();
        } else if (character == ',') {
            flush();
            tokens.emplace_back(",");
        } else if (std::isalnum(character) != 0 || character == '_' ||
                   character == '-') {
            current.push_back(static_cast<char>(character));
        } else {
            valid = false;
            return {};
        }
    }
    flush();
    return tokens;
}

bool canonical_unsigned(const std::string& text) {
    if (text == "0") {
        return true;
    }
    if (text.empty() || text.front() == '0') {
        return false;
    }
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

bool canonical_integer(const std::string& text) {
    if (text == "0") {
        return true;
    }
    if (!text.empty() && text.front() == '-') {
        return text.size() > 1 && text[1] != '0' &&
            canonical_unsigned(text.substr(1));
    }
    return canonical_unsigned(text);
}

bool parse_size(const std::string& text, std::size_t& value) {
    if (!canonical_unsigned(text)) {
        return false;
    }
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool valid_register(const std::string& text) {
    return text == "R0" || text == "R1" || text == "R2" || text == "R3";
}

bool valid_label(const std::string& text) {
    if (text.empty() ||
        !(std::isalpha(static_cast<unsigned char>(text.front())) != 0 ||
          text.front() == '_')) {
        return false;
    }
    for (const unsigned char character : text) {
        if (std::isalnum(character) == 0 && character != '_') {
            return false;
        }
    }
    static const std::set<std::string> reserved = {
        "CONST", "LOAD", "STORE", "MOV", "ADD", "SUB", "MUL", "DIV",
        "CMP_EQ", "CMP_NE", "CMP_LT", "CMP_LE", "CMP_GT", "CMP_GE",
        "JMP", "JZ", "LABEL", "PRINT", "HALT"
    };
    return reserved.count(text) == 0;
}

} // namespace

bool LciValidator::validate(
    const std::string_view text,
    const std::filesystem::path& output_path,
    common::DiagnosticBag& diagnostics) const {
    struct SourceLine {
        std::size_t number;
        std::string text;
    };
    std::vector<SourceLine> lines;
    std::istringstream input{std::string(text)};
    std::string raw_line;
    std::size_t line_number = 0;
    while (std::getline(input, raw_line)) {
        ++line_number;
        auto cleaned = trim(std::move(raw_line));
        if (!cleaned.empty()) {
            lines.push_back(SourceLine{line_number, std::move(cleaned)});
        }
    }

    auto report = [&](const std::size_t line, const std::string& message) {
        diagnostics.report(
            common::DiagnosticCategory::CodeGeneration,
            common::DiagnosticSeverity::Error,
            "LCERO_IR 1 invalido: " + message,
            common::SourceLocation{output_path, line, 1});
        return false;
    };

    if (lines.size() < 3) {
        return report(0, "faltan encabezado, directiva MEM o instrucciones.");
    }

    bool lexical_valid = true;
    const auto header = tokenize(lines[0].text, lexical_valid);
    if (!lexical_valid || header != std::vector<std::string>{"LCERO_IR", "1"}) {
        return report(lines[0].number, "el encabezado debe ser 'LCERO_IR 1'.");
    }

    const auto memory = tokenize(lines[1].text, lexical_valid);
    std::size_t memory_size = 0;
    if (!lexical_valid || memory.size() != 2 || memory[0] != "MEM" ||
        !parse_size(memory[1], memory_size)) {
        return report(lines[1].number, "la segunda linea debe ser 'MEM n'.");
    }

    std::set<std::string> labels;
    std::vector<std::pair<std::string, std::size_t>> references;
    bool has_halt = false;

    auto valid_slot = [&](const std::string& token) {
        std::size_t slot = 0;
        return parse_size(token, slot) && slot < memory_size;
    };
    auto comma = [](const std::string& token) { return token == ","; };

    for (std::size_t index = 2; index < lines.size(); ++index) {
        const auto& source_line = lines[index];
        const auto tokens = tokenize(source_line.text, lexical_valid);
        if (!lexical_valid || tokens.empty()) {
            return report(source_line.number, "caracter o linea no admitida.");
        }

        const auto& mnemonic = tokens[0];
        bool valid = false;
        if (mnemonic == "CONST") {
            valid = tokens.size() == 4 && valid_register(tokens[1]) &&
                comma(tokens[2]) && canonical_integer(tokens[3]);
        } else if (mnemonic == "LOAD") {
            valid = tokens.size() == 4 && valid_register(tokens[1]) &&
                comma(tokens[2]) && valid_slot(tokens[3]);
        } else if (mnemonic == "STORE") {
            valid = tokens.size() == 4 && valid_slot(tokens[1]) &&
                comma(tokens[2]) && valid_register(tokens[3]);
        } else if (mnemonic == "MOV") {
            valid = tokens.size() == 4 && valid_register(tokens[1]) &&
                comma(tokens[2]) && valid_register(tokens[3]);
        } else if (mnemonic == "ADD" || mnemonic == "SUB" ||
                   mnemonic == "MUL" || mnemonic == "DIV" ||
                   mnemonic == "CMP_EQ" || mnemonic == "CMP_NE" ||
                   mnemonic == "CMP_LT" || mnemonic == "CMP_LE" ||
                   mnemonic == "CMP_GT" || mnemonic == "CMP_GE") {
            valid = tokens.size() == 6 && valid_register(tokens[1]) &&
                comma(tokens[2]) && valid_register(tokens[3]) &&
                comma(tokens[4]) && valid_register(tokens[5]);
        } else if (mnemonic == "JMP") {
            valid = tokens.size() == 2 && valid_label(tokens[1]);
            if (valid) {
                references.emplace_back(tokens[1], source_line.number);
            }
        } else if (mnemonic == "JZ") {
            valid = tokens.size() == 4 && valid_register(tokens[1]) &&
                comma(tokens[2]) && valid_label(tokens[3]);
            if (valid) {
                references.emplace_back(tokens[3], source_line.number);
            }
        } else if (mnemonic == "LABEL") {
            valid = tokens.size() == 2 && valid_label(tokens[1]);
            if (valid && !labels.insert(tokens[1]).second) {
                return report(source_line.number, "etiqueta duplicada: '" +
                    tokens[1] + "'.");
            }
        } else if (mnemonic == "PRINT") {
            valid = tokens.size() == 2 && valid_register(tokens[1]);
        } else if (mnemonic == "HALT") {
            valid = tokens.size() == 1;
            has_halt = has_halt || valid;
        }

        if (!valid) {
            return report(source_line.number,
                          "instruccion, aridad u operando no autorizado.");
        }
    }

    for (const auto& reference : references) {
        if (labels.count(reference.first) == 0) {
            return report(reference.second,
                          "etiqueta no definida: '" + reference.first + "'.");
        }
    }
    if (!has_halt) {
        return report(0, "el archivo no contiene HALT.");
    }
    return true;
}

} // namespace lcero::ir
