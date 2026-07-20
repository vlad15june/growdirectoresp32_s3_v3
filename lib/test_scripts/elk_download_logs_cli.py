"""
Download all logs from ELK for a specific module within a time window.
Parameterized CLI variant of elk_download_logs.py.

Usage:
  python3 elk_download_logs_cli.py \
      --uid 9C:13:9E:9A:52:98 \
      --from 2026-05-06T00:00:00.000Z \
      --to   2026-05-06T04:00:00.000Z \
      --out  doc/log-analysis/2026-05-06-foo/logs.txt
"""

import argparse
import os
import sys
from datetime import datetime, timezone, timedelta

import urllib3
import requests

ELK_URL = "https://5.161.157.194:9200"
API_KEY = "bHNBWFY1d0I5Q2NsVXhHWE5VdUw6Y29vdVU5ZW1TWkNfMzR4RGYzWTJOZw=="

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

HEADERS = {
    "Content-Type": "application/json",
    "Authorization": f"ApiKey {API_KEY}",
}

PAGE_SIZE = 5000
DEFAULT_MAX_RESULTS = 200000


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
    mqtt = src.get("mqtt")
    if isinstance(mqtt, dict):
        topic = mqtt.get("topic", "")
    else:
        topic = src.get("mqtt.topic", "")
    message = src.get("message", "")
    return f"{ts} | {topic} | {message}"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Download ELK logs for a module into a file.")
    p.add_argument("--uid", required=True, help="Module UID (MAC), e.g. 9C:13:9E:9A:52:98")
    p.add_argument("--from", dest="time_from", default=None,
                   help="Time range start in ISO-8601 (e.g. 2026-05-06T00:00:00.000Z). "
                        "If omitted, uses now - --minutes.")
    p.add_argument("--to", dest="time_to", default=None,
                   help="Time range end in ISO-8601. If omitted, uses now.")
    p.add_argument("--minutes", type=int, default=15,
                   help="Fallback window in minutes when --from/--to omitted (default 15).")
    p.add_argument("--out", required=True, help="Output file path. Parent dirs are created.")
    p.add_argument("--max", dest="max_results", type=int, default=DEFAULT_MAX_RESULTS,
                   help=f"Max log lines (default {DEFAULT_MAX_RESULTS}).")
    return p.parse_args()


def main() -> int:
    args = parse_args()

    if args.time_from and args.time_to:
        t_from, t_to = args.time_from, args.time_to
    else:
        now = datetime.now(timezone.utc)
        t_from = (now - timedelta(minutes=args.minutes)).strftime("%Y-%m-%dT%H:%M:%S")
        t_to = now.strftime("%Y-%m-%dT%H:%M:%S")

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    print(f"Module: {args.uid}")
    print(f"Time range: {t_from} -> {t_to}")
    print(f"Output: {args.out}")
    print("Fetching logs...")

    hits = fetch_logs(args.uid, t_from, t_to, args.max_results)

    if not hits:
        print("No logs found.")
        with open(args.out, "w") as f:
            f.write("")
        return 0

    lines = [format_hit(h) for h in hits]
    with open(args.out, "w") as f:
        f.write("\n".join(lines))

    print(f"\nSaved {len(lines)} log entries to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
