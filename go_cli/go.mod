module qminiwasm/go_cli

go 1.26

require (
	github.com/fatih/color v1.19.0
	github.com/pelletier/go-toml/v2 v2.3.0
	github.com/rodaine/table v1.3.1
	github.com/spf13/cobra v1.8.1
)

replace qminiwasm/agents => ../agents

require (
	github.com/inconshreveable/mousetrap v1.1.0 // indirect
	github.com/mattn/go-colorable v0.1.14 // indirect
	github.com/mattn/go-isatty v0.0.20 // indirect
	github.com/spf13/pflag v1.0.5 // indirect
	golang.org/x/sys v0.42.0 // indirect
	qminiwasm/agents v0.0.0-00010101000000-000000000000 // indirect
)
