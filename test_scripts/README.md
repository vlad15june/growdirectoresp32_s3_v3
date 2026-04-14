# ELK Log Downloader

## Setup (macOS/Linux)

```bash
cd test_scripts
python3 -m venv .venv
source .venv/bin/activate
pip install requests
```

## Setup (Windows)

```powershell
cd test_scripts
python -m venv .venv
.venv\Scripts\activate
pip install requests
```

## Configuration

Edit constants in `elk_download_logs.py`:

| Constant | Description | Default |
|----------|-------------|---------|
| `MODULE_UID` | MAC-address of the module | `9C:13:9E:99:41:6C` |
| `TIME_WINDOW_MINUTES` | How many minutes back from now | `15` |
| `TIME_FROM` / `TIME_TO` | Explicit time range (set both or leave `None`) | `None` |
| `OUTPUT_FILE` | Output file name (`None` = auto with timestamp) | `None` |
| `MAX_RESULTS` | Max log entries to fetch | `10000` |

## Usage examples

### Default: last 15 minutes for configured module

```bash
# macOS/Linux
python elk_download_logs.py

# Windows
python elk_download_logs.py
```

### Change module UID (edit in code)

```python
MODULE_UID = "AA:BB:CC:DD:EE:FF"
```

### Change time window to 60 minutes

```python
TIME_WINDOW_MINUTES = 60
```

### Use explicit time range

```python
TIME_FROM = "2026-04-02T10:00:00"
TIME_TO   = "2026-04-02T10:30:00"
```

### Save to specific file

```python
OUTPUT_FILE = "my_debug_session.txt"
```

## Output

Logs are printed to stdout and saved to `module_logs_YYYYMMDD_HHMMSS.txt`.

Format: `timestamp | mqtt_topic | message`
