#' Agent Configuration
#' 
#' Configuration class for agent settings.
#' @export
AgentConfig <- R6::R6Class(" AgentConfig\,
 public = list(
 name = NULL,
 description = NULL,
 system_prompt = NULL,
 tools = NULL,
 max_iterations = 5,
 improvement_cycle_frequency = \daily\,
 rate_limit_rpm = 15,
 rate_limit_tpm = 1000000,
 cache_ttl = 3600,
 
 initialize = function(name, description, system_prompt, tools = list(), 
 max_iterations = 5, rate_limit_rpm = 15, 
 rate_limit_tpm = 1000000, cache_ttl = 3600) {
 self <- name
 self <- description
 self <- system_prompt
 self <- tools
 self <- max_iterations
 self <- rate_limit_rpm
 self <- rate_limit_tpm
 self <- cache_ttl
 },
 
 to_list = function() {
 list(
 name = self,
 description = self,
 system_prompt = self,
 tools = self,
 max_iterations = self,
 improvement_cycle_frequency = self,
 rate_limit_rpm = self,
 rate_limit_tpm = self,
 cache_ttl = self
 )
 }
 )
)
