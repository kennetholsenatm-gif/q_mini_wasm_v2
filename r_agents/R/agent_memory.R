#' Agent Memory System
#' 
#' Provides persistent storage for agent patterns and improvements.
#' @export
AgentMemory <- R6::R6Class(" AgentMemory\,
 public = list(
 #' @field short_term Short-term memory (current task context)
 short_term = list(),
 
 #' @field long_term Long-term memory (persistent patterns)
 long_term = list(),
 
 #' @field patterns List of detected patterns
 patterns = list(),
 
 #' @field improvements List of implemented improvements
 improvements = list(),
 
 #' @description
 #' Create a new AgentMemory instance
 initialize = function() {
 self <- list()
 self <- list()
 self <- list()
 self <- list()
 },
 
 #' @description
 #' Store a detected pattern
 #' @param pattern Pattern to store (list with type, description, confidence)
 store_pattern = function(pattern) {
 pattern <- format(Sys.time(), \%Y-%m-%dT%H:%M:%S\)
 self <- c(self, list(pattern))
 if (length(self) > 100) {
 self <- tail(self, 100)
 }
 },
 
 #' @description
 #' Store an implemented improvement
 #' @param improvement Improvement to store
 store_improvement = function(improvement) {
 improvement <- format(Sys.time(), \%Y-%m-%dT%H:%M:%S\)
 self <- c(self, list(improvement))
 if (length(self) > 50) {
 self <- tail(self, 50)
 }
 },
 
 #' @description
 #' Get recent patterns
 #' @param limit Maximum number of patterns to return
 get_recent_patterns = function(limit = 10) {
 if (length(self) == 0) return(list())
 return(tail(self, min(limit, length(self))))
 },
 
 #' @description
 #' Get recent improvements
 #' @param limit Maximum number of improvements to return
 get_recent_improvements = function(limit = 10) {
 if (length(self) == 0) return(list())
 return(tail(self, min(limit, length(self))))
 },
 
 #' @description
 #' Save memory to JSON file
 #' @param filepath Path to save memory
 save_to_file = function(filepath) {
 memory_data <- list(
 short_term = self,
 long_term = self,
 patterns = self,
 improvements = self
 )
 jsonlite::write_json(memory_data, filepath, pretty = TRUE, auto_unbox = TRUE)
 },
 
 #' @description
 #' Load memory from JSON file
 #' @param filepath Path to load memory from
 load_from_file = function(filepath) {
 if (!file.exists(filepath)) return(invisible(self))
 memory_data <- jsonlite::from_json(filepath, simplifyVector = FALSE)
 self <- memory_data
 self <- memory_data
 self <- memory_data
 self <- memory_data
 return(invisible(self))
 }
 )
)
