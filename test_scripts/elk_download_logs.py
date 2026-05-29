"""
Download all logs from ELK for a specific module within a time window.
Outputs logs to stdout and saves to a file.
"""

import json
import urllib3
import sys
from datetime import datetime, timezone, timedelta

import requests

# ──────────────── CONFIGURATION ────────────────
# MODULE_UID = "9C:13:9E:9A:51:9C"
# MODULE_UID = "58:CF:79:9E:5F:C2"
MODULE_UID = "9C:13:9E:9A:52:98"
TIME_WINDOW_MINUTES = 15
# Set to None to use "last N minutes from now", or set explicit range:
# TIME_FROM = "2026-04-02T10:00:00"
# TIME_TO   = "2026-04-02T10:15:00"
# TIME_FROM = None
# TIME_TO = None
TIME_FROM = "2026-05-06T00:00:00.000Z"
TIME_TO = "2026-05-13T23:59:59.999Z"

OUTPUT_FILE = None  # auto-generated with timestamp
MAX_RESULTS = 200000
# ────────────────────────────────────────────────

ELK_URL = "https://5.161.157.194:9200"
API_KEY = "bHNBWFY1d0I5Q2NsVXhHWE5VdUw6Y29vdVU5ZW1TWkNfMzR4RGYzWTJOZw=="

# Suppress InsecureRequestWarning
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

HEADERS = {
    "Content-Type": "application/json",
    "Authorization": f"ApiKey {API_KEY}",
}


PAGE_SIZE = 5000


def build_query(module_uid: str, time_from: str, time_to: str, size: int, search_after=None) -> dict:
    q = {
        "query": {
            "bool": {
                "must": [
                    {"match_phrase": {"mqtt.topic": module_uid}},
                    {"range": {"@timestamp": {"gte": time_from, "lte": time_to}}},
                ]
            }
        },
        "size": size,
        "sort": [{"@timestamp": {"order": "asc"}}, {"_doc": {"order": "asc"}}],
    }
    if search_after is not None:
        q["search_after"] = search_after
    return q


def fetch_logs(module_uid: str, time_from: str, time_to: str, max_results: int) -> list:
    all_hits = []
    search_after = None
    total_reported = None
    while len(all_hits) < max_results:
        size = min(PAGE_SIZE, max_results - len(all_hits))
        query = build_query(module_uid, time_from, time_to, size, search_after)
        resp = requests.get(
            f"{ELK_URL}/mqtt-*/_search",
            headers=HEADERS,
            json=query,
            verify=False,
            timeout=60,
        )
        resp.raise_for_status()
        data = resp.json()
        hits_obj = data.get("hits", {})
        if total_reported is None:
            total_reported = hits_obj.get("total", {}).get("value", 0)
            print(f"Total hits: {total_reported}")
        page = hits_obj.get("hits", [])
        if not page:
            break
        all_hits.extend(page)
        print(f"  fetched {len(all_hits)}/{total_reported}")
        if len(page) < size:
            break
        search_after = page[-1].get("sort")
        if not search_after:
            break
    return all_hits


def format_hit(hit: dict) -> str:
    src = hit.get("_source", {})
    ts = src.get("@timestamp", "")
    topic = src.get("mqtt", {}).get("topic", "") if isinstance(src.get("mqtt"), dict) else src.get("mqtt.topic", "")
    message = src.get("message", "")
    return f"{ts} | {topic} | {message}"


def main():
    if TIME_FROM and TIME_TO:
        t_from, t_to = TIME_FROM, TIME_TO
    else:
        now = datetime.now(timezone.utc)
        t_from = (now - timedelta(minutes=TIME_WINDOW_MINUTES)).strftime("%Y-%m-%dT%H:%M:%S")
        t_to = now.strftime("%Y-%m-%dT%H:%M:%S")

    timestamp_str = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_file = OUTPUT_FILE or f"module_logs_{timestamp_str}.txt"

    print(f"Module: {MODULE_UID}")
    print(f"Time range: {t_from} -> {t_to}")
    print(f"Fetching logs...")

    hits = fetch_logs(MODULE_UID, t_from, t_to, MAX_RESULTS)

    if not hits:
        print("No logs found.")
        return

    lines = [format_hit(h) for h in hits]

    with open(output_file, "w") as f:
        f.write("\n".join(lines))

    print(f"\nSaved {len(lines)} log entries to {output_file}")


if __name__ == "__main__":
    main()
