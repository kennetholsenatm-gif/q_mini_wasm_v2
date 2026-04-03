"""
Research Alignment Agent for Code-to-Research Validation

This agent validates code implementations against research papers and documentation.
It ensures that the code faithfully implements the algorithms, methods, and approaches
described in the referenced research.

Output Languages: C++, DLL (compiled C++), Go, R
"""

import asyncio
import json
import re
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
from dataclasses import dataclass, field
from enum import Enum

import structlog

from .base_agent import BaseAgent, AgentConfig, TaskResult

logger = structlog.get_logger()


class AlignmentStatus(Enum):
    """Status of code alignment with research."""
    ALIGNED = "aligned"
    PARTIALLY_ALIGNED = "partially_aligned"
    NOT_ALIGNED = "not_aligned"
    UNKNOWN = "unknown"


class ResearchSection(Enum):
    """Sections typically found in research papers."""
    ABSTRACT = "abstract"
    INTRODUCTION = "introduction"
    METHODOLOGY = "methodology"
    ALGORITHM = "algorithm"
    IMPLEMENTATION = "implementation"
    RESULTS = "results"
    CONCLUSION = "conclusion"


@dataclass
class ResearchExtract:
    """Extracted information from research document."""
    title: str
    authors: List[str] = field(default_factory=list)
    algorithms: List[Dict[str, Any]] = field(default_factory=list)
    methods: List[Dict[str, Any]] = field(default_factory=list)
    parameters: Dict[str, Any] = field(default_factory=dict)
    expected_results: Dict[str, Any] = field(default_factory=dict)
    key_formulas: List[str] = field(default_factory=list)
    pseudocode: List[str] = field(default_factory=list)
    timestamp: datetime = field(default_factory=datetime.now)


@dataclass
class CodeAnalysis:
    """Analysis of existing code."""
    file_path: str
    language: str
    functions: List[Dict[str, Any]] = field(default_factory=list)
    classes: List[Dict[str, Any]] = field(default_factory=list)
    algorithms_implemented: List[str] = field(default_factory=list)
    dependencies: List[str] = field(default_factory=list)
    complexity_metrics: Dict[str, float] = field(default_factory=dict)


@dataclass
class AlignmentResult:
    """Result of alignment check between code and research."""
    overall_status: AlignmentStatus
    alignment_score: float
    aligned_components: List[Dict[str, Any]] = field(default_factory=list)
    missing_components: List[Dict[str, Any]] = field(default_factory=list)
    divergent_components: List[Dict[str, Any]] = field(default_factory=list)
    suggestions: List[Dict[str, Any]] = field(default_factory=list)
    code_recommendations: Dict[str, str] = field(default_factory=dict)


@dataclass
class SupportedLanguage:
    """Supported programming languages for code generation."""
    name: str
    extension: str
    file_types: List[str] = field(default_factory=list)
    
    @staticmethod
    def get_supported() -> List['SupportedLanguage']:
        """Get list of supported languages."""
        return [
            SupportedLanguage("C++", ".cpp", [".cpp", ".cc", ".cxx", ".h", ".hpp"]),
            SupportedLanguage("Go", ".go", [".go"]),
            SupportedLanguage("R", ".R", [".R", ".r"]),
            SupportedLanguage("DLL", ".dll", [".dll"]),
        ]


class ResearchAlignmentAgent(BaseAgent):
    """
    Research alignment agent for validating code against research.
    
    This agent:
    1. Parses research documents (PDFs, markdown, text)
    2. Extracts algorithms, methods, and specifications
    3. Analyzes existing code implementations
    4. Compares implementations to research specifications
    5. Generates alignment reports
    6. Produces code suggestions in C++, Go, and R
    
    Supported output languages: C++, DLL (compiled C++), Go, R
    """
    
    def __init__(self, config: AgentConfig, llm_service=None):
        super().__init__(config, llm_service)
        self._research_cache: Dict[str, ResearchExtract] = {}
        self._code_analyses: Dict[str, CodeAnalysis] = {}
        self._alignment_history: List[AlignmentResult] = []
        self._supported_languages = SupportedLanguage.get_supported()
    
    @property
    def supported_languages(self) -> List[str]:
        """Get list of supported language names."""
        return [lang.name for lang in self._supported_languages]
    
    async def execute_task(self, task: Dict[str, Any]) -> TaskResult:
        """Execute a research alignment task."""
        task_type = task.get("type", "unknown")
        parameters = task.get("parameters", {})
        
        self.logger.info("Executing alignment task", task_type=task_type)
        self.state = "running"
        
        try:
            if task_type == "parse_research":
                result = await self._parse_research_document(parameters)
            elif task_type == "analyze_code":
                result = await self._analyze_codebase(parameters)
            elif task_type == "check_alignment":
                result = await self._check_alignment(parameters)
            elif task_type == "generate_aligned_code":
                result = await self._generate_aligned_code(parameters)
            elif task_type == "full_alignment_analysis":
                result = await self._full_alignment_analysis(parameters)
            else:
                return TaskResult(success=False, errors=[f"Unknown task type: {task_type}"])
            
            self.state = "idle"
            return TaskResult(success=True, data=result)
            
        except Exception as e:
            self.logger.error("Alignment task failed", error=str(e))
            self.state = "error"
            return TaskResult(success=False, errors=[str(e)])
    
    def _detect_language(self, extension: str) -> str:
        """Detect programming language from file extension."""
        extension = extension.lower()
        for lang in self._supported_languages:
            if extension in lang.file_types:
                return lang.name
        
        lang_map = {
            ".cpp": "C++", ".cc": "C++", ".cxx": "C++",
            ".h": "C++", ".hpp": "C++", ".hxx": "C++",
            ".go": "Go", ".r": "R", ".R": "R", ".dll": "DLL",
        }
        return lang_map.get(extension, "unknown")
    
    async def _parse_research_document(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """Parse a research document to extract key information."""
        document_path = parameters.get("document_path")
        document_content = parameters.get("document_content", "")
        
        if document_path:
            path = Path(document_path)
            if path.exists():
                document_content = path.read_text(encoding="utf-8")
            else:
                raise FileNotFoundError(f"Research document not found: {document_path}")
        
        if not document_content:
            raise ValueError("No document content provided")
        
        extraction_prompt = f"""
Analyze the following research document and extract key information.

Document Content:
{document_content[:10000]}

Extract the following in JSON format:
{{
    "title": "Paper title",
    "authors": ["Author names"],
    "algorithms": [
        {{
            "name": "Algorithm name",
            "description": "Brief description",
            "steps": ["Step 1", "Step 2", ...],
            "complexity": "Time/space complexity if mentioned",
            "parameters": {{"param_name": "description"}}
        }}
    ],
    "methods": [
        {{
            "name": "Method name",
            "description": "Brief description",
            "inputs": ["Input types"],
            "outputs": ["Output types"]
        }}
    ],
    "key_formulas": ["Mathematical formulas mentioned"],
    "pseudocode": ["Pseudocode blocks if present"],
    "expected_results": {{
        "metrics": ["Performance metrics mentioned"],
        "benchmarks": ["Benchmark results if any"]
    }}
}}

Be thorough and extract ALL algorithms and methods mentioned.
"""
        
        analysis = await self.llm_service.analyze_with_context(
            analysis_prompt=extraction_prompt,
            context={"document_length": len(document_content)},
            system_prompt=self.config.system_prompt
        )
        
        extract = ResearchExtract(
            title=analysis.get("title", "Unknown"),
            authors=analysis.get("authors", []),
            algorithms=analysis.get("algorithms", []),
            methods=analysis.get("methods", []),
            key_formulas=analysis.get("key_formulas", []),
            pseudocode=analysis.get("pseudocode", []),
            expected_results=analysis.get("expected_results", {})
        )
        
        cache_key = document_path or f"inline_{datetime.now().timestamp()}"
        self._research_cache[cache_key] = extract
        
        self.memory.store_pattern({
            "type": "research_parsed",
            "title": extract.title,
            "algorithms_count": len(extract.algorithms),
            "methods_count": len(extract.methods),
            "timestamp": datetime.now().isoformat()
        })
        
        return {
            "extract": {
                "title": extract.title,
                "authors": extract.authors,
                "algorithms": extract.algorithms,
                "methods": extract.methods,
                "key_formulas": extract.key_formulas,
                "pseudocode": extract.pseudocode,
                "expected_results": extract.expected_results
            },
            "cache_key": cache_key
        }
    
    async def _check_alignment(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """Check alignment between research and code."""
        research_key = parameters.get("research_key")
        code_key = parameters.get("code_key")
        research_content = parameters.get("research_content")
        code_content = parameters.get("code_content")
        language = parameters.get("language", "C++")
        
        research_extract = self._research_cache.get(research_key)
        code_analysis = self._code_analyses.get(code_key)
        
        if not research_extract and research_content:
            parse_result = await self._parse_research_document({
                "document_content": research_content
            })
            research_extract = self._research_cache.get(parse_result["cache_key"])
        
        if not code_analysis and code_content:
            analyze_result = await self._analyze_codebase({
                "code_content": code_content,
                "language": language
            })
            code_analysis = self._code_analyses.get(analyze_result["cache_key"])
        
        if not research_extract:
            raise ValueError("No research document available for alignment check")
        
        comparison_prompt = f"""
Compare the following research specification with the code implementation.

Research Specification:
Title: {research_extract.title}
Algorithms: {json.dumps(research_extract.algorithms, indent=2)}
Methods: {json.dumps(research_extract.methods, indent=2)}

Code Implementation:
Language: {code_analysis.language if code_analysis else language}
Functions: {json.dumps(code_analysis.functions if code_analysis else [], indent=2)}

Provide alignment analysis in JSON format with:
- overall_status (aligned|partially_aligned|not_aligned)
- alignment_score (0.0-1.0)
- aligned_components
- missing_components
- divergent_components
- suggestions
"""
        
        analysis = await self.llm_service.analyze_with_context(
            analysis_prompt=comparison_prompt,
            context={
                "research_title": research_extract.title,
                "code_language": code_analysis.language if code_analysis else language
            },
            system_prompt=self.config.system_prompt
        )
        
        status_str = analysis.get("overall_status", "unknown")
        status = AlignmentStatus.PARTIALLY_ALIGNED
        if status_str == "aligned":
            status = AlignmentStatus.ALIGNED
        elif status_str == "not_aligned":
            status = AlignmentStatus.NOT_ALIGNED
        
        alignment_result = AlignmentResult(
            overall_status=status,
            alignment_score=analysis.get("alignment_score", 0.5),
            aligned_components=analysis.get("aligned_components", []),
            missing_components=analysis.get("missing_components", []),
            divergent_components=analysis.get("divergent_components", []),
            suggestions=analysis.get("suggestions", [])
        )
        
        self._alignment_history.append(alignment_result)
        
        self.memory.store_pattern({
            "type": "alignment_check",
            "status": status.value,
            "score": alignment_result.alignment_score,
            "missing_count": len(alignment_result.missing_components),
            "timestamp": datetime.now().isoformat()
        })
        
        return {
            "alignment": {
                "status": alignment_result.overall_status.value,
                "score": alignment_result.alignment_score,
                "aligned_components": alignment_result.aligned_components,
                "missing_components": alignment_result.missing_components,
                "divergent_components": alignment_result.divergent_components,
                "suggestions": alignment_result.suggestions
            }
        }
    
    async def _analyze_codebase(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """Analyze existing codebase to understand current implementations."""
        code_path = parameters.get("code_path")
        code_content = parameters.get("code_content", "")
        language = parameters.get("language", "unknown")
        
        if code_path:
            path = Path(code_path)
            if path.exists():
                code_content = path.read_text(encoding="utf-8")
                language = self._detect_language(path.suffix)
            else:
                raise FileNotFoundError(f"Code file not found: {code_path}")
        
        if not code_content:
            raise ValueError("No code content provided")
        
        if language not in ["C++", "Go", "R", "DLL"]:
            self.logger.warning(f"Language {language} not in supported list")
        
        analysis_prompt = f"""
Analyze the following {language} code and extract its structure.

Code:
{code_content[:8000]}

Extract in JSON format:
{{
    "functions": [
        {{
            "name": "Function name",
            "signature": "Full signature",
            "description": "What it does",
            "parameters": ["Parameter types and names"],
            "return_type": "Return type"
        }}
    ],
    "classes": [
        {{
            "name": "Class name",
            "methods": ["Method names"],
            "members": ["Member variables"],
            "description": "Purpose of class"
        }}
    ],
    "algorithms_implemented": ["Names of algorithms or patterns used"],
    "dependencies": ["External dependencies or includes"],
    "complexity_metrics": {{
        "lines_of_code": number,
        "cyclomatic_complexity": estimated_number,
        "functions_count": number
    }}
}}
"""
        
        analysis = await self.llm_service.analyze_with_context(
            analysis_prompt=analysis_prompt,
            context={"language": language, "code_length": len(code_content)},
            system_prompt=self.config.system_prompt
        )
        
        code_analysis = CodeAnalysis(
            file_path=code_path or "inline",
            language=language,
            functions=analysis.get("functions", []),
            classes=analysis.get("classes", []),
            algorithms_implemented=analysis.get("algorithms_implemented", []),
            dependencies=analysis.get("dependencies", []),
            complexity_metrics=analysis.get("complexity_metrics", {})
        )
        
        cache_key = code_path or f"code_{datetime.now().timestamp()}"
        self._code_analyses[cache_key] = code_analysis
        
        self.memory.store_pattern({
            "type": "code_analyzed",
            "language": language,
            "functions_count": len(code_analysis.functions),
            "algorithms": code_analysis.algorithms_implemented,
            "timestamp": datetime.now().isoformat()
        })
        
        return {
            "analysis": {
                "file_path": code_analysis.file_path,
                "language": code_analysis.language,
                "functions": code_analysis.functions,
                "classes": code_analysis.classes,
                "algorithms_implemented": code_analysis.algorithms_implemented,
                "dependencies": code_analysis.dependencies,
                "complexity_metrics": code_analysis.complexity_metrics
            },
            "cache_key": cache_key
        }
    
    async def _generate_aligned_code(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """Generate code aligned with research specifications."""
        research_key = parameters.get("research_key")
        target_language = parameters.get("target_language", "C++")
        component = parameters.get("component", "")
        existing_code = parameters.get("existing_code", "")
        research_content = parameters.get("research_content")
        
        supported = ["C++", "Go", "R", "DLL"]
        if target_language not in supported:
            raise ValueError(f"Language {target_language} not supported. Supported: {', '.join(supported)}")
        
        research_extract = self._research_cache.get(research_key)
        if not research_extract and research_content:
            parse_result = await self._parse_research_document({"document_content": research_content})
            research_extract = self._research_cache.get(parse_result["cache_key"])
        
        if not research_extract:
            raise ValueError("No research document available for code generation")
        
        generation_prompt = f"""
Generate {target_language} code implementing research specification.

Research: {research_extract.title}
Algorithms: {json.dumps(research_extract.algorithms[:3], indent=2)}
Methods: {json.dumps(research_extract.methods[:3], indent=2)}

Requirements:
1. Clean, well-documented {target_language} code
2. Follow {target_language} best practices
3. Include error handling
4. Match research specification

JSON format: {{"code": "Implementation", "header_file": "Header if C++", "build_instructions": "How to build", "dependencies": ["Libraries"]}}
"""
        
        generation = await self.llm_service.analyze_with_context(
            analysis_prompt=generation_prompt,
            context={"language": target_language, "component": component},
            system_prompt=self.config.system_prompt
        )
        
        self.memory.store_improvement({
            "type": "code_generated",
            "language": target_language,
            "timestamp": datetime.now().isoformat()
        })
        
        return {
            "generated_code": {
                "language": target_language,
                "code": generation.get("code", ""),
                "header_file": generation.get("header_file", ""),
                "build_instructions": generation.get("build_instructions", ""),
                "dependencies": generation.get("dependencies", [])
            }
        }
    
    async def _full_alignment_analysis(self, parameters: Dict[str, Any]) -> Dict[str, Any]:
        """Perform a complete alignment analysis workflow."""
        research_path = parameters.get("research_path")
        research_content = parameters.get("research_content")
        code_paths = parameters.get("code_paths", [])
        code_contents = parameters.get("code_contents", {})
        
        self.logger.info("Step 1: Parsing research document")
        parse_result = await self._parse_research_document({
            "document_path": research_path,
            "document_content": research_content
        })
        research_key = parse_result["cache_key"]
        
        self.logger.info("Step 2: Analyzing code files")
        code_analyses = []
        for code_path in code_paths:
            try:
                analyze_result = await self._analyze_codebase({"code_path": code_path})
                code_analyses.append(analyze_result)
            except Exception as e:
                self.logger.warning(f"Failed to analyze {code_path}", error=str(e))
        
        for lang, content in code_contents.items():
            try:
                analyze_result = await self._analyze_codebase({
                    "code_content": content,
                    "language": lang
                })
                code_analyses.append(analyze_result)
            except Exception as e:
                self.logger.warning(f"Failed to analyze {lang} code", error=str(e))
        
        self.logger.info("Step 3: Checking alignment")
        alignments = []
        for code_analysis in code_analyses:
            try:
                alignment = await self._check_alignment({
                    "research_key": research_key,
                    "code_key": code_analysis["cache_key"]
                })
                alignments.append(alignment)
            except Exception as e:
                self.logger.warning("Alignment check failed", error=str(e))
        
        code_suggestions = {}
        self.logger.info("Step 4: Generating code suggestions")
        for language in ["C++", "Go", "R"]:
            try:
                suggestion = await self._generate_aligned_code({
                    "research_key": research_key,
                    "target_language": language
                })
                code_suggestions[language] = suggestion["generated_code"]
            except Exception as e:
                self.logger.warning(f"Failed to generate {language} code", error=str(e))
        
        avg_score = (
            sum(a["alignment"]["score"] for a in alignments) / len(alignments)
            if alignments else 0
        )
        
        return {
            "timestamp": datetime.now().isoformat(),
            "research": parse_result["extract"],
            "code_analyses": [a["analysis"] for a in code_analyses],
            "alignments": [a["alignment"] for a in alignments],
            "code_suggestions": code_suggestions,
            "summary": {
                "total_files_analyzed": len(code_analyses),
                "average_alignment": avg_score,
                "supported_languages": self.supported_languages
            }
        }
    
    async def analyze_performance(self) -> Dict[str, Any]:
        """Analyze research alignment agent's own performance."""
        recent = self._alignment_history[-10:] if self._alignment_history else []
        scores = [a.alignment_score for a in recent]
        avg_score = sum(scores) / len(scores) if scores else 0
        
        return {
            "tasks_completed": self._task_count,
            "research_documents_cached": len(self._research_cache),
            "code_files_analyzed": len(self._code_analyses),
            "alignment_checks_performed": len(self._alignment_history),
            "average_alignment_score": avg_score,
            "supported_languages": self.supported_languages,
            "patterns_stored": len(self.memory.patterns),
        }
    
    async def suggest_improvements(self) -> List[Dict[str, Any]]:
        """Suggest improvements for the research alignment agent."""
        suggestions = []
        
        if len(self._research_cache) < 2:
            suggestions.append({
                "type": "data_collection",
                "description": "Parse more research documents",
                "priority": "high",
                "estimated_impact": 0.3
            })
        
        if len(self._alignment_history) < 5:
            suggestions.append({
                "type": "validation",
                "description": "Perform more alignment checks",
                "priority": "medium",
                "estimated_impact": 0.2
            })
        
        return suggestions
    
    def get_alignment_history(self, limit: int = 10) -> List[Dict[str, Any]]:
        """Get recent alignment history."""
        recent = self._alignment_history[-limit:] if self._alignment_history else []
        return [
            {
                "status": a.overall_status.value,
                "score": a.alignment_score,
                "missing_count": len(a.missing_components),
            }
            for a in recent
        ]
    
    def get_research_cache_summary(self) -> Dict[str, Any]:
        """Get summary of cached research documents."""
        return {
            key: {
                "title": extract.title,
                "algorithms_count": len(extract.algorithms),
                "methods_count": len(extract.methods),
            }
            for key, extract in self._research_cache.items()
        }