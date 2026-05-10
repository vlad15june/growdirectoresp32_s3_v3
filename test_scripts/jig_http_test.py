#!/usr/bin/env python3
import sys, urllib.request, urllib.error

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.1"

# (method, path, expected_substring_or_None)
TESTS = [
    ("GET",  "/read_mac_address",            "{mac="),
    ("GET",  "/read_version",                None),
    ("GET",  "/check_fs",                    "done"),
    ("GET",  "/read_module_type",            "SENSOR"),
    ("GET",  "/init_rtc3028",                "RTC 3028 online!"),
    ("GET",  "/read_rtc3028",                "RTC time is"),
    ("GET",  "/led_g?ch=0&v=1",              "{LEDG0=1}"),
    ("GET",  "/led_g?ch=0&v=0",              "{LEDG0=0}"),
    ("GET",  "/led_r?ch=0&v=1",              "{LEDR0=1}"),
    ("GET",  "/led_r?ch=0&v=0",              "{LEDR0=0}"),
    ("GET",  "/set_test_button?v=5",         "Ok"),
    ("GET",  "/get_test_button",             None),  # pressed/not pressed depends on jig
    ("GET",  "/sensor_ps_status?ch=0",       "SPS0=1"),
    ("GET",  "/ps_en?ch=0&v=1",              "{PS0=1}"),
    ("GET",  "/ps_en?ch=0&v=0",              "{PS0=0}"),
    ("GET",  "/listen485?v=3",               "Listening"),
    # ("POST", "/set_test_success",            "Operation mode"),
]

def request(method, url):
    req = urllib.request.Request(url, method=method)
    try:
        with urllib.request.urlopen(req, timeout=8) as r:
            return r.status, r.read().decode("utf-8", errors="replace").strip()
    except urllib.error.HTTPError as e:
        return e.code, (e.read().decode("utf-8", errors="replace").strip() if e.fp else "")
    except Exception as e:
        return None, f"<{type(e).__name__}: {e}>"

passed = failed = 0
print(f"target http://{HOST}\n")
for method, path, expected in TESTS:
    url = f"http://{HOST}{path}"
    status, body = request(method, url)
    exp_str = expected if expected is not None else "<any>"
    if expected is None:
        ok = status == 200
    else:
        ok = status == 200 and expected in body
    tag = "PASS" if ok else "FAIL"
    print(f"[{tag}] {method:4} {path}")
    print(f"        expected : {exp_str}")
    print(f"        actual   : HTTP {status} | {body!r}")
    if ok:
        passed += 1
    else:
        failed += 1

print(f"\n{passed} passed, {failed} failed of {len(TESTS)}")
sys.exit(0 if failed == 0 else 1)
