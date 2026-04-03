# Environment Secrets Management

This project includes tools for managing environment variables and secrets across projects.

## Overview

The environment management system provides:

1. **Environment Loader** (`agents/config/env_loader.py`) - Loads environment variables from `.env` files
2. **Environment Scanner** (`scripts/scan_env_secrets.py`) - Scans directories for environment files and variables
3. **Template Files** (`.env.example`) - Example environment file with all required variables

## Quick Start

### 1. Set up your environment file

Copy the example file to create your own `.env` file:

```bash
# Windows
copy .env.example .env

# Linux/Mac
cp .env.example .env
```

Then edit `.env` with your actual values:

```bash
# Required
GEMINI_API_KEY=your_actual_api_key_here

# Optional
SLACK_WEBHOOK_URL=your_slack_webhook_url_here
```

### 2. Use the environment loader in your code

```python
from agents.config import env_config, get_env, require_env

# Get environment variable with default
api_key = env_config.get("GEMINI_API_KEY", "default_key")

# Get required environment variable (raises error if not set)
api_key = require_env("GEMINI_API_KEY")

# Get typed values
debug_mode = env_config.get_bool("DEBUG", False)
port = env_config.get_int("PORT", 8080)
timeout = env_config.get_float("TIMEOUT", 30.0)
```

### 3. Scan for environment files

Use the scanner to find environment files across projects:

```bash
# Scan current directory
python scripts/scan_env_secrets.py

# Scan specific directory
python scripts/scan_env_secrets.py /path/to/projects

# Scan C:\GitHub directory
python scripts/scan_env_secrets.py --github

# Generate JSON report
python scripts/scan_env_secrets.py --github -o env_report.json
```

## Environment Files

### Common Environment File Patterns

The system looks for these files:

- `.env` - Main environment file
- `.env.local` - Local overrides
- `.env.development` - Development environment
- `.env.production` - Production environment
- `.env.test` - Test environment
- `.env.example` - Example/template file

### File Locations

Environment files are searched in:

1. Current directory
2. Parent directories (up to 3 levels)
3. Common locations:
   - `config/.env`
   - `secrets/.env`
   - `env/.env`

## Environment Variables Used in This Project

Based on code analysis, these environment variables are used:

### Required Variables

- `GEMINI_API_KEY` - Google Gemini API key for LLM services

### Optional Variables

- `SLACK_WEBHOOK_URL` - Slack webhook for notifications
- `DEBUG` - Enable debug mode (true/false)
- `LOG_LEVEL` - Logging level (INFO, DEBUG, ERROR)
- `CACHE_DIR` - Custom cache directory
- `AGENTS_CONFIG_PATH` - Custom configuration file path

### Rate Limiting Overrides

- `GEMINI_RPM` - Requests per minute (default: 15)
- `GEMINI_TPM` - Tokens per minute (default: 1,000,000)
- `GEMINI_RPD` - Requests per day (default: 1,500)

## Best Practices

### 1. Never commit `.env` files

Add `.env` to your `.gitignore`:

```gitignore
# Environment files
.env
.env.local
.env.*.local
.env.development
.env.production
.env.test
```

### 2. Use `.env.example` as a template

Maintain a `.env.example` file with all required variables (without values) so other developers know what to set.

### 3. Validate required variables

Use `require_env()` for critical variables to fail fast:

```python
api_key = require_env("GEMINI_API_KEY")  # Raises ValueError if not set
```

### 4. Use typed getters

```python
# Good
debug = env_config.get_bool("DEBUG", False)
port = env_config.get_int("PORT", 8080)

# Avoid
debug = os.getenv("DEBUG") == "true"  # Manual parsing
```

## Troubleshooting

### Environment variables not loading

1. Check if `.env` file exists in current directory
2. Verify file format (KEY=VALUE, no spaces around =)
3. Check for syntax errors in the file
4. Ensure `python-dotenv` is installed

### Scanner not finding files

1. Verify directory path exists
2. Check file permissions
3. Ensure files match expected patterns

### API key not found

1. Verify `GEMINI_API_KEY` is set in `.env` or system environment
2. Check for typos in variable names
3. Restart your application after changing `.env`

## Security Notes

- Never log or print actual secret values
- Use environment variables for all secrets
- Rotate API keys regularly
- Use different keys for development/production
- Consider using a secrets manager for production deployments

## Integration with CI/CD

### GitHub Actions

```yaml
env:
  GEMINI_API_KEY: ${{ secrets.GEMINI_API_KEY }}
```

### Docker

```dockerfile
# Copy environment file
COPY .env.example .env

# Or use build args
ARG GEMINI_API_KEY
ENV GEMINI_API_KEY=$GEMINI_API_KEY
```

## Support

For issues with environment management:

1. Check the logs for detailed error messages
2. Run the scanner to diagnose issues
3. Verify all required variables are set
4. Check file permissions and paths