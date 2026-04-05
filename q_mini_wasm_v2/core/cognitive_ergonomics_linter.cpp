#include "cognitive_ergonomics_linter.hpp"
#include "toml.hpp"
#include <fstream>
#include <sstream>
#include <regex>

namespace qminiwasm {

CognitiveErgonomicsLinter::CognitiveErgonomicsLinter() {}

std::vector<CognitiveErgonomicsViolation> CognitiveErgonomicsLinter::lintToml(const std::string& tomlContent) {
    violations.clear();
    
    try {
        auto config = toml::parse(tomlContent);
        nlohmann::json jsonObj = nlohmann::json::parse(config.serialize());
        checkMillersLaw(jsonObj, "root");
        checkChunkingPrinciple(jsonObj, "root");
        checkVisualHierarchy(jsonObj, "root");
        checkProgressiveDisclosure(jsonObj, "root");
    } catch (const std::exception& e) {
        addViolation("Valid TOML Parsing", "critical", "root", 
                    "Failed to parse TOML content: " + std::string(e.what()),
                    "Fix TOML syntax errors before ergonomic analysis");
    }
    
    return violations;
}

std::vector<CognitiveErgonomicsViolation> CognitiveErgonomicsLinter::lintMarkdown(const std::string& markdownContent, const std::string& filePath) {
    violations.clear();
    checkMarkdownLineLength(markdownContent, filePath);
    checkMarkdownHeadingDepth(markdownContent, filePath);
    return violations;
}

std::vector<CognitiveErgonomicsViolation> CognitiveErgonomicsLinter::lintCode(const std::string& codeContent, const std::string& filePath) {
    violations.clear();
    checkCodeComplexity(codeContent, filePath);
    return violations;
}

std::vector<CognitiveErgonomicsViolation> CognitiveErgonomicsLinter::getViolations() const {
    return violations;
}

void CognitiveErgonomicsLinter::addViolation(const std::string& principle, const std::string& severity, 
                                            const std::string& path, const std::string& description, 
                                            const std::string& recommendation) {
    CognitiveErgonomicsViolation violation;
    violation.principle = principle;
    violation.severity = severity;
    violation.path = path;
    violation.description = description;
    violation.recommendation = recommendation;
    violations.push_back(violation);
}

void CognitiveErgonomicsLinter::checkMillersLaw(const nlohmann::json& obj, const std::string& path) {
    if (obj.is_object()) {
        size_t itemCount = obj.size();
        if (itemCount > 9) {
            addViolation("Miller's Law (7±2 Working Memory Limit)", "high", path,
                        "Section contains more than 9 ungrouped items",
                        "Group related items into logical sub-sections (chunks) of 5-9 items maximum");
        }
        
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.value().is_object()) {
                checkMillersLaw(it.value(), path + "." + it.key());
            }
        }
    }
}

void CognitiveErgonomicsLinter::checkChunkingPrinciple(const nlohmann::json& obj, const std::string& path) {
    if (obj.is_object()) {
        std::map<std::string, int> commonPrefixes;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            std::string key = it.key();
            size_t underscorePos = key.find('_');
            if (underscorePos != std::string::npos) {
                std::string prefix = key.substr(0, underscorePos);
                commonPrefixes[prefix]++;
            }
        }
        
        for (const auto& pair : commonPrefixes) {
            if (pair.second >= 4) {
                addViolation("Structural Chunking Principle", "medium", path,
                            "Multiple parameters share common prefix indicating missing grouping",
                            "Create a nested table for '" + pair.first + "_*' parameters to form logical chunks");
            }
        }
    }
}

void CognitiveErgonomicsLinter::checkVisualHierarchy(const nlohmann::json& obj, const std::string& path) {
    if (obj.is_object()) {
        bool hasCritical = false;
        bool hasTrivial = false;
        
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            std::string key = it.key();
            std::string lower = key;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            
            if (lower.find("enabled") != std::string::npos || lower.find("critical") != std::string::npos ||
                lower.find("timeout") != std::string::npos || lower.find("api_key") != std::string::npos) {
                hasCritical = true;
            }
            if (lower.find("color") != std::string::npos || lower.find("theme") != std::string::npos ||
                lower.find("debug") != std::string::npos || lower.find("verbose") != std::string::npos) {
                hasTrivial = true;
            }
        }
        
        if (hasCritical && hasTrivial) {
            addViolation("Visual Hierarchy Principle", "medium", path,
                        "Critical operational parameters mixed with cosmetic/debug settings",
                        "Separate critical path configuration into separate section before cosmetic options");
        }
    }
}

void CognitiveErgonomicsLinter::checkProgressiveDisclosure(const nlohmann::json& obj, const std::string& path) {
    if (obj.is_object()) {
        int advancedCount = 0;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            std::string key = it.key();
            std::string lower = key;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            
            if (lower.find("advanced") != std::string::npos || lower.find("experimental") != std::string::npos ||
                lower.find("expert") != std::string::npos || lower.find("beta") != std::string::npos) {
                advancedCount++;
            }
        }
        
        if (advancedCount >= 3 && path != "root.advanced") {
            addViolation("Progressive Disclosure Principle", "low", path,
                        "Multiple advanced options present at root level",
                        "Move all advanced/experimental settings into an [advanced] nested table");
        }
    }
}

void CognitiveErgonomicsLinter::checkMarkdownLineLength(const std::string& content, const std::string& filePath) {
    std::istringstream stream(content);
    std::string line;
    size_t lineNumber = 0;
    const size_t MAX_LINE_LENGTH = 75;
    
    while (std::getline(stream, line)) {
        lineNumber++;
        std::string stripped = line;
        stripped.erase(stripped.find_last_not_of(" \t\n\r") + 1);
        
        if (stripped.length() > MAX_LINE_LENGTH) {
            addViolation("Line Length Principle", "medium", filePath,
                        "Line " + std::to_string(lineNumber) + " is " + std::to_string(stripped.length()) + 
                        " chars (max " + std::to_string(MAX_LINE_LENGTH) + ")",
                        "Break long lines into multiple lines of maximum " + std::to_string(MAX_LINE_LENGTH) + " characters");
        }
    }
}

void CognitiveErgonomicsLinter::checkMarkdownHeadingDepth(const std::string& content, const std::string& filePath) {
    std::istringstream stream(content);
    std::string line;
    size_t lineNumber = 0;
    const size_t MAX_NAV_DEPTH = 3;
    
    while (std::getline(stream, line)) {
        lineNumber++;
        std::string stripped = line;
        stripped.erase(stripped.find_last_not_of(" \t\n\r") + 1);
        
        if (stripped.find('#') == 0) {
            size_t depth = stripped.find_first_not_of('#');
            if (depth > MAX_NAV_DEPTH) {
                addViolation("Navigation Depth Principle", "medium", filePath,
                            "Heading depth is " + std::to_string(depth) + 
                            " (max " + std::to_string(MAX_NAV_DEPTH) + ")",
                            "Reduce heading depth to maximum " + std::to_string(MAX_NAV_DEPTH) + " levels");
            }
        }
    }
}

void CognitiveErgonomicsLinter::checkCodeComplexity(const std::string& content, const std::string& filePath) {
    // Simple complexity check: count nested blocks
    int nestedBlocks = 0;
    int maxNested = 0;
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        std::string stripped = line;
        stripped.erase(stripped.find_last_not_of(" \t\n\r") + 1);
        
        if (stripped.find('{') != std::string::npos) {
            nestedBlocks++;
            if (nestedBlocks > maxNested) {
                maxNested = nestedBlocks;
            }
        }
        if (stripped.find('}') != std::string::npos) {
            nestedBlocks--;
        }
    }
    
    if (maxNested > 3) {
        addViolation("Code Complexity Principle", "medium", filePath,
                    "Code contains " + std::to_string(maxNested) + 
                    " levels of nesting (max 3)",
                    "Refactor nested code into separate functions to reduce complexity");
    }
}

} // namespace qminiwasm