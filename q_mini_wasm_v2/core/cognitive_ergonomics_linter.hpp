#ifndef COGNITIVE_ERGONOMICS_LINTER_HPP
#define COGNITIVE_ERGONOMICS_LINTER_HPP

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace qminiwasm {

struct CognitiveErgonomicsViolation {
    std::string principle;
    std::string severity;
    std::string path;
    std::string description;
    std::string recommendation;
};

class CognitiveErgonomicsLinter {
public:
    CognitiveErgonomicsLinter();
    
    std::vector<CognitiveErgonomicsViolation> lintToml(const std::string& tomlContent);
    std::vector<CognitiveErgonomicsViolation> lintMarkdown(const std::string& markdownContent, const std::string& filePath);
    std::vector<CognitiveErgonomicsViolation> lintCode(const std::string& codeContent, const std::string& filePath);
    
    std::vector<CognitiveErgonomicsViolation> getViolations() const;
    
private:
    std::vector<CognitiveErgonomicsViolation> violations;
    
    void addViolation(const std::string& principle, const std::string& severity, 
                     const std::string& path, const std::string& description, 
                     const std::string& recommendation);
    
    void checkMillersLaw(const nlohmann::json& obj, const std::string& path);
    void checkChunkingPrinciple(const nlohmann::json& obj, const std::string& path);
    void checkVisualHierarchy(const nlohmann::json& obj, const std::string& path);
    void checkProgressiveDisclosure(const nlohmann::json& obj, const std::string& path);
    void checkMarkdownLineLength(const std::string& content, const std::string& filePath);
    void checkMarkdownHeadingDepth(const std::string& content, const std::string& filePath);
    void checkCodeComplexity(const std::string& content, const std::string& filePath);
};

} // namespace qminiwasm

#endif // COGNITIVE_ERGONOMICS_LINTER_HPP