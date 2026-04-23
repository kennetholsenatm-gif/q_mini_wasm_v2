# External Data Setup

This document describes how to keep large data files (datasets, weights, checkpoints) outside the git repository.

## Problem

Git repositories should not contain large binary files or generated datasets:
- Project Gutenberg books (~1000 files)
- Binary datasets (.t3b files, 50-100MB+)
- Model weights and checkpoints (potentially GBs)
- Training logs and artifacts

## Solution

Store all data in an **external directory** outside the repository:

```
C:\q_mini_data\                 (Windows external)
├── datasets\                     # All datasets
│   ├── book_pg_*.txt            # Gutenberg books
│   ├── *.jsonl                  # JSONL datasets
│   ├── *.t3b                    # Binary trit datasets
│   └── external\                # Symlink to here
├── weights\                      # Model weights
├── checkpoints\                  # Training checkpoints
└── logs\                        # Training logs
```

## Quick Setup

Run the setup script (PowerShell):

```powershell
.\scripts\setup_external_data.ps1
```

This will:
1. Create `C:\q_mini_data\` directory structure
2. Move all Gutenberg books and datasets there
3. Create a local config file with the new paths
4. Keep `datasets/text_to_trit_binary.py` in git (the converter script)

## Manual Setup

If you prefer a different location:

```powershell
# 1. Create your data directory
mkdir D:\ML_Data\q_mini\datasets

# 2. Move data files
Move-Item datasets\*.txt D:\ML_Data\q_mini\datasets\
Move-Item datasets\*.jsonl D:\ML_Data\q_mini\datasets\
Move-Item datasets\*.t3b D:\ML_Data\q_mini\datasets\

# 3. Update training_config.toml
[dataset]
path = "D:\ML_Data\q_mini\datasets\seed_large_v2.t3b"
```

## Configuration

The trainer loads paths in this priority:

1. **Environment variable**: `Q_MINI_DATA_PATH`
2. **Local config**: `config/local_data_paths.toml` (git-ignored)
3. **Main config**: `config/training_config.toml`

### Local Config Example

Create `config/local_data_paths.toml` (automatically git-ignored):

```toml
# Local data paths - this file is git-ignored
[dataset]
path = 'C:\q_mini_data\datasets\seed_large_v2.t3b'
format = 't3b'

[paths]
dataset_dir = 'C:\q_mini_data\datasets'
checkpoint_dir = 'C:\q_mini_data\checkpoints'
weights_dir = 'C:\q_mini_data\weights'
logs_dir = 'C:\q_mini_data\logs'
```

## Git Repository Contents

After migration, the repository contains:

```
q_mini_wasm_v2/
├── datasets/
│   ├── text_to_trit_binary.py    # Converter script (in git)
│   └── general/                   # Small sample datasets
├── config/
│   ├── training_config.toml       # Default paths
│   └── local_data_paths.toml      # Your local paths (ignored)
├── scripts/
│   └── setup_external_data.ps1    # Setup script
└── docs/
    └── DATA_SETUP.md              # This file
```

## What's Ignored

The following are in `.gitignore` and never committed:

```
datasets/book_pg_*.txt
datasets/*.jsonl
datasets/*.t3b
datasets/*/
weights/
checkpoints/
logs/
config/local_data_paths.toml
```

## Cross-Platform Paths

| Platform | Recommended Path |
|----------|------------------|
| Windows | `C:\q_mini_data\` or `D:\ML_Data\q_mini\` |
| Linux | `/var/lib/q_mini/data/` or `~/q_mini_data/` |
| macOS | `~/Library/Application Support/q_mini/data/` |

## Benefits

1. **Lean repository**: Git history stays small and fast
2. **Multiple datasets**: Keep multiple dataset versions externally
3. **Backup strategy**: Back up `C:\q_mini_data\` separately from code
4. **CI/CD friendly**: GitHub Actions won't download massive datasets
5. **Collaboration**: Team members use their own external data paths
