#' Utility functions for r_agents package

#' Null-coalescing operator
#' @param a First value
#' @param b Default value
#' @return a if not NULL, otherwise b
%||% <- function(a, b) if (!is.null(a)) a else b

#' Load configuration from file
#' @param path Path to configuration file
#' @return Configuration list
load_config <- function(path) {
  if (!is.null(path) && file.exists(path)) {
    return(jsonlite::from_json(path, simplifyVector = FALSE))
  }
  return(NULL)
}

#' Save configuration to file
#' @param config Configuration list
#' @param path Path to save configuration
save_config <- function(config, path) {
  dir.create(dirname(path), recursive = TRUE, showWarnings = FALSE)
  jsonlite::write_json(config, path, pretty = TRUE, auto_unbox = TRUE)
}
