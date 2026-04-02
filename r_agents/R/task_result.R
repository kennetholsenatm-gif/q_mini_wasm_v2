#' Task Result
#' 
#' Result of an agent task execution.
#' @export
TaskResult <- R6::R6Class(" TaskResult\,
 public = list(
 success = TRUE,
 data = list(),
 errors = list(),
 metrics = list(),
 timestamp = NULL,
 
 initialize = function(success = TRUE, data = list(), errors = list(), metrics = list()) {
 self <- success
 self <- data
 self <- errors
 self <- metrics
 self <- Sys.time()
 },
 
 has_errors = function() {
 length(self) > 0
 },
 
 to_list = function() {
 list(
 success = self,
 data = self,
 errors = self,
 metrics = self,
 timestamp = format(self, \%Y-%m-%dT%H:%M:%S\)
 )
 }
 )
)
