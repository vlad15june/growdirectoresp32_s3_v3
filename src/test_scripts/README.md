# ELK Log Downloader

## Setup

```bash
cd src/test_scripts
python3 -m venv .venv
source .venv/bin/activate
pip install requests
```

## Usage

1. Edit constants in `elk_download_logs.py`:
   - `MODULE_UID` — MAC-address of the module (e.g. `9C:13:9E:99:41:6C`)
   - `TIME_WINDOW_MINUTES` — how many minutes back from now (default: 15)
   - `TIME_FROM` / `TIME_TO` — set both for an explicit time range, or leave `None` for relative window

2. Run:
```bash
python elk_download_logs.py
```

Logs are printed to stdout and saved to `module_logs_YYYYMMDD_HHMMSS.txt`.
