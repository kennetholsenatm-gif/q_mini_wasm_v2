# Windows Development Environment

## IMPORTANT: This is a Windows Development Environment

All agents and tools must understand that this development environment runs on **Windows**. Please follow these guidelines:

## Environment Details

- **Platform**: Windows 10/11
- **Shell**: PowerShell and Command Prompt (cmd)
- **Git**: Git for Windows
- **Python**: Python 3.x for Windows
- **Editor**: VS Code or similar

## Command Execution Guidelines

### DO USE:
1. **PowerShell scripts** (`.ps1` files)
2. **Batch files** (`.bat` or `.cmd` files)
3. **Windows-style paths** with backslashes: `C:\Users\...\`
4. **PowerShell commands**: `Get-Content`, `Set-Location`, etc.
5. **Windows environment variables**: `%USERPROFILE%`, `%PATH%`, etc.

### DO NOT USE:
1. **Bash scripts** (`.sh` files) - Will not work natively
2. **Unix/Linux commands**: `ls`, `cat`, `grep` (unless using Git Bash)
3. **Forward slashes** in paths: `/c/Users/...` (use backslashes)
4. **Unix-style environment variables**: `$HOME`, `$PATH` (use Windows style)
5. **chmod**, **sudo**, or other Unix-specific commands

## Available Scripts for This Environment

### For Wiki Population Task:
1. **`execute-windows.ps1`** - PowerShell script (RECOMMENDED)
2. **`simple-execute.bat`** - Batch file alternative
3. **`execute-task.bat`** - Another batch file option

### How to Run:

#### PowerShell (Recommended):
```powershell
.\execute-windows.ps1
```

#### Command Prompt:
```cmd
simple-execute.bat
```

#### Or:
```cmd
execute-task.bat
```

## Git Commands in Windows

### Use Windows-style Git:
```powershell
git status
git add .
git commit -m "message"
git push
```

### Path Examples:
```powershell
# Correct
C:\Users\kenne\.cline\worktrees\c08b3\q_mini_wasm_v2

# Incorrect (Unix-style)
/c/Users/kenne/.cline/worktrees/c08b3/q_mini_wasm_v2
```

## Python in Windows

### Running Python Scripts:
```powershell
# Correct
python generate_wiki_simple.py

# Or with full path
python C:\Users\kenne\.cline\worktrees\c08b3\q_mini_wasm_v2\generate_wiki_simple.py
```

### PowerShell Execution Policy:
If you get execution policy errors:
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

## File Paths in Windows

### Use Backslashes:
```powershell
# Correct
Get-Content "C:\Users\kenne\.cline\worktrees\c08b3\q_mini_wasm_v2\README.md"

# Incorrect (Unix-style)
Get-Content "/c/Users/kenne/.cline/worktrees/c08b3/q_mini_wasm_v2/README.md"
```

## Common Windows Commands

### File Operations:
```powershell
# List files
Get-ChildItem
dir

# Change directory
Set-Location "C:\Path\To\Directory"
cd "C:\Path\To\Directory"

# View file content
Get-Content file.txt
type file.txt

# Copy files
Copy-Item source.txt destination.txt
copy source.txt destination.txt
```

## Environment Variables

### Windows-style:
```powershell
$env:USERPROFILE
$env:PATH
$env:TEMP
```

### Or in CMD:
```cmd
%USERPROFILE%
%PATH%
%TEMP%
```

## Troubleshooting

### PowerShell Script Won't Run:
1. Check execution policy: `Get-ExecutionPolicy`
2. Set to RemoteSigned: `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser`
3. Or run with: `powershell -ExecutionPolicy Bypass -File script.ps1`

### Git Commands Not Working:
1. Ensure Git for Windows is installed
2. Check Git is in PATH: `git --version`
3. Use Git Bash if needed for Unix-style commands

### Python Not Found:
1. Ensure Python is installed
2. Check Python is in PATH: `python --version`
3. Use full path if needed: `C:\Python39\python.exe`

## Summary for Agents

**REMEMBER**: This is a **Windows** environment. Always:
- Use PowerShell or CMD
- Use Windows-style paths with backslashes
- Use `.ps1` or `.bat` scripts
- Avoid Unix/Linux-specific commands
- Use Windows environment variables

The wiki population task should be executed using:
```powershell
.\execute-windows.ps1
```

This will properly commit the changes to the `q_mini_wasm_v2` branch on Windows.