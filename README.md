# infer

A minimal CLI tool for piping anything into an LLM, written in pure C with minimal dependencies. The goal is to be as minimal as possible.

```bash
ps aux | infer "what's eating memory"
dmesg | infer "any hardware errors?"
git log --oneline -20 | infer "summarize recent changes"
```

It reads from stdin, sends to an LLM, and outputs plain text to stdout. Perfect for shell pipelines.

## Installation

### Prerequisites

- `libcurl`
- A C compiler (gcc/clang)

On Ubuntu/Debian:
```bash
sudo apt install libcurl4-openssl-dev
```

On macOS:
```bash
brew install curl
```

### Build

```bash
# Clone and build
git clone https://github.com/chethanreddy1/infer.git
cd infer
gcc -o infer infer.c -lcurl

# Install system-wide
sudo cp infer /usr/local/bin/
sudo chmod +x /usr/local/bin/infer
```

### Configuration

Set environment variables in your shell profile (`~/.bashrc`, `~/.zshrc`, etc):

```bash
export INFER_API_URL="https://api.openai.com/v1/chat/completions"
export INFER_API_KEY="sk-your-api-key-here"
export INFER_MODEL="gpt-4"
```

Works with any OpenAI-compatible API (OpenAI, Anthropic, local llama.cpp servers, etc).


For local llama.cpp:
```bash
export INFER_API_URL="http://localhost:8080/v1/chat/completions"
export INFER_API_KEY="not-needed"
export INFER_MODEL="llama-3.2"
```

Reload your shell or run `source ~/.bashrc` to apply changes.

## Usage

Basic syntax:
```bash
infer "your question"              # Ask a question
command | infer "your question"    # Analyze command output
```

### Arguments & Shell Expansion

You can pass arguments without quotes, and `infer` will join them with spaces:

```bash
infer how do I exit vim
# equivalent to:
infer "how do I exit vim"
```

Be careful with shell special characters like `*`, `?`, `|`, `>`, etc. The shell expands these *before* passing them to the program.

```bash
# BAD: The shell expands '*' to all files in the current directory
infer explain what does * do in C? 
# logic: infer "explain" "what" "does" "file1.txt" "file2.c" ... "do" "in" "C?"

# GOOD: Quote special characters to prevent expansion
infer "explain what does * do in C?"
```

### Examples

**System diagnostics:**
```bash
# Check memory hogs
ps aux | head -n 20 | infer "what's using the most memory"

# Analyze disk usage
df -h | infer "am I running out of space anywhere?"

# Check for errors in kernel logs
dmesg | tail -100 | infer "any hardware issues?"
```

**Log analysis:**
```bash
# Apache/Nginx logs
tail -100 /var/log/nginx/error.log | infer "summarize errors"

# Journal logs
journalctl -n 200 | infer "anything concerning?"

# Application logs
tail -50 app.log | infer "what's causing the failures?"
```

**Git workflows:**
```bash
# Summarize recent work
git log --oneline -20 | infer "summarize recent changes"

# Review uncommitted changes
git diff | infer "what did I change?"

# Explain a complex diff
git show abc123 | infer "explain this commit"
```

**File analysis:**
```bash
# Analyze configs
cat nginx.conf | infer "any security issues?"

# Quick code review
cat script.py | infer "any bugs or improvements?"

# Parse structured data
cat data.json | infer "extract all email addresses"
```

**Quick references:**
```bash
# No pipe needed - just ask
infer "what's the tar command to extract .tar.gz?"
infer "how do I attach to a tmux session by name?"
infer "regex to match email addresses?"
```

**Network debugging:**
```bash
# Check connections
netstat -tuln | infer "any suspicious ports open?"

# Analyze traffic
tcpdump -c 50 | infer "summarize network activity"
```

**Chaining with other tools:**
```bash
# Find and analyze large files
find . -type f -size +100M | infer "what are these files?"

# Complex pipeline
docker ps | infer "any containers using too much memory?" | tee report.txt
```

## Tips

- The tool outputs plain text, so you can pipe it further: `command | infer "question" | grep important`
- For long outputs, use `head` or `tail` to limit context: `journalctl | tail -500 | infer "issues?"`
- Works great in scripts: `#!/bin/bash\nps aux | infer "memory usage summary" > report.txt`

- Switch models on the fly: `INFER_MODEL="gpt-3.5-turbo" infer "quick question"`

## Configuration Lookup

`infer` reads configuration from environment variables:
- `INFER_API_URL` - API endpoint
- `INFER_API_KEY` - Your API key  
- `INFER_MODEL` - Model name

You can override them per-command:
```bash
INFER_MODEL="claude-opus-4" infer "complex analysis task"
```


## Contributing

PRs welcome! Keep it minimal and Unix-y.

---
