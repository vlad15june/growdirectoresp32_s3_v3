#!/usr/bin/env python3
import argparse
import json
import os
import re
import select
import subprocess
import sys
import time

CMD_TOPIC = "GrowDirectorMqttTopic/{uid}"
CMD_RESULT_TOPIC = "hardware/command_result/{uid}"
RAW_IO_TOPIC = "hardware/raw_io_result/{uid}"

SENSOR_DECODERS = {
    "xf700a": {
        0x0009: ("temp", lambda v: v / 100.0 - 40, "C"),
        0x000A: ("humidity", lambda v: v / 100.0, "%"),
        0x000B: ("pressure", lambda v: v / 10.0, "hPa"),
        0x000C: ("wind_speed", lambda v: v / 100.0, "m/s"),
        0x000D: ("wind_dir", lambda v: v / 10.0, "deg"),
        0x000E: ("rain_10min", lambda v: v / 100.0, "mm"),
        0x000F: ("solar", lambda v: float(v), "W/m2"),
        0x0010: ("lux", lambda v: v / 100.0, "klux"),
        0x0012: ("co2", lambda v: float(v), "ppm"),
    },
}


def die(msg, code=2):
    print(msg, file=sys.stderr)
    sys.exit(code)


def load_globals(path):
    creds = {}
    try:
        text = open(path, "r", errors="ignore").read()
    except OSError:
        return creds
    m = re.search(r"IPAddress\s+mqttBroker\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", text)
    if m:
        creds["broker"] = ".".join(m.groups())
    m = re.search(r"mqttPort\s*=\s*(\d+)", text)
    if m:
        creds["port"] = m.group(1)
    m = re.search(r'mqttUser\s*=\s*"([^"]*)"', text)
    if m:
        creds["user"] = m.group(1)
    m = re.search(r'mqttPassword\s*=\s*"([^"]*)"', text)
    if m:
        creds["password"] = m.group(1)
    return creds


def resolve_conn(args):
    g = load_globals(args.globals)
    conn = {
        "broker": os.environ.get("GD_BROKER") or args.broker or g.get("broker"),
        "port": os.environ.get("GD_PORT") or args.port or g.get("port"),
        "user": os.environ.get("GD_USER") or args.user or g.get("user"),
        "password": os.environ.get("GD_PASS") or args.password or g.get("password"),
        "uid": os.environ.get("GD_UID") or args.uid,
    }
    for k in ("broker", "port", "user", "password"):
        if not conn[k]:
            die(f"missing {k}: not in {args.globals}, env, or flags")
    if not conn["uid"]:
        die("missing uid: pass --uid or set GD_UID")
    return conn


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def decode_read_frame(hexs):
    try:
        raw = bytes.fromhex(hexs)
    except ValueError:
        return {"ok": False, "note": "not hex"}
    if len(raw) < 5:
        return {"ok": False, "note": "frame too short"}
    addr, func = raw[0], raw[1]
    if func & 0x80:
        return {"ok": False, "addr": addr, "func": func, "exception": raw[2] if len(raw) > 2 else None}
    bytecount = raw[2]
    payload = raw[3:3 + bytecount]
    crc_recv = raw[-2:]
    crc_calc = crc16(raw[:-2])
    words = [(payload[i] << 8) | payload[i + 1] for i in range(0, len(payload) - 1, 2)]
    return {
        "ok": True,
        "addr": addr,
        "func": func,
        "bytecount": bytecount,
        "words": words,
        "crc_ok": crc_recv == crc_calc,
    }


def pub_args(conn, topic, payload):
    return [
        "mosquitto_pub", "-h", conn["broker"], "-p", str(conn["port"]),
        "-u", conn["user"], "-P", conn["password"],
        "-t", topic, "-m", payload,
    ]


def sub_args(conn, uid, window):
    return [
        "mosquitto_sub", "-h", conn["broker"], "-p", str(conn["port"]),
        "-u", conn["user"], "-P", conn["password"], "-v", "-W", str(window),
        "-t", RAW_IO_TOPIC.format(uid=uid),
        "-t", CMD_RESULT_TOPIC.format(uid=uid),
    ]


def probe(conn, payload_obj, timeout, print_cmd=False, show_pass=False):
    uid = conn["uid"]
    payload = json.dumps(payload_obj, separators=(",", ":"))
    cmd_topic = CMD_TOPIC.format(uid=uid)
    if print_cmd:
        shown = dict(conn)
        if not show_pass:
            shown["password"] = "***"
        print("# sub:", " ".join(sub_args({**conn, "password": shown["password"]}, uid, int(timeout) + 1)))
        print("# pub:", " ".join(pub_args({**conn, "password": shown["password"]}, cmd_topic, payload)))
    window = max(2, int(timeout) + 1)
    sub = subprocess.Popen(sub_args(conn, uid, window), stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, text=True, bufsize=1)
    time.sleep(0.5)
    subprocess.run(pub_args(conn, cmd_topic, payload), stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=False)
    deadline = time.time() + timeout
    result = {"status": "timeout", "raw_io": None, "cmd_result": None}
    while time.time() < deadline:
        remaining = deadline - time.time()
        rlist, _, _ = select.select([sub.stdout], [], [], max(0.05, remaining))
        if not rlist:
            continue
        line = sub.stdout.readline()
        if not line:
            break
        line = line.strip()
        if " " not in line:
            continue
        topic, _, body = line.partition(" ")
        try:
            obj = json.loads(body)
        except json.JSONDecodeError:
            continue
        if topic.startswith("hardware/command_result/"):
            result["cmd_result"] = obj
            if obj.get("status") == "error" and "Invalid" in str(obj.get("message", "")):
                result["status"] = "error"
                result["message"] = obj.get("message")
                break
        elif topic.startswith("hardware/raw_io_result/"):
            if "channel" in obj and obj["channel"] != payload_obj.get("channel"):
                continue
            result["raw_io"] = obj
            if "data" in obj:
                result["status"] = "ok"
            elif "error" in obj:
                result["status"] = "no_response"
                result["message"] = obj.get("error")
            break
    try:
        sub.terminate()
        sub.wait(timeout=2)
    except Exception:
        pass
    return result


def probe_retry(conn, payload_obj, timeout, retries=0, print_cmd=False, show_pass=False):
    res = None
    for attempt in range(retries + 1):
        res = probe(conn, payload_obj, timeout, print_cmd and attempt == 0, show_pass)
        if res["status"] in ("ok", "error"):
            return res
    return res


def fmt_line(prefix, res, decode=None):
    st = res["status"]
    if st == "ok":
        data = res["raw_io"].get("data", "")
        dec = decode_read_frame(data)
        extra = ""
        if dec.get("ok"):
            words = dec["words"]
            crc = "crc=ok" if dec["crc_ok"] else "crc=BAD"
            extra = f" addr=0x{dec['addr']:02X} words={words} {crc}"
            if decode and words:
                eng = decode_engineering(decode, res.get("_register"), words)
                if eng:
                    extra += f" {eng}"
        elif "exception" in dec:
            extra = f" MODBUS_EXCEPTION code={dec['exception']}"
        return f"{prefix} | OK  frame={data}{extra}"
    if st == "no_response":
        return f"{prefix} | TIMEOUT ({res.get('message', 'no response')})"
    if st == "error":
        return f"{prefix} | ERROR: {res.get('message')}"
    return f"{prefix} | TIMEOUT (no message)"


def decode_engineering(model, register, words):
    if model is None or register is None:
        return ""
    regmap = SENSOR_DECODERS.get(model)
    if not regmap:
        return ""
    out = []
    for i, w in enumerate(words):
        info = regmap.get(register + i)
        if info:
            name, conv, unit = info
            out.append(f"{name}={conv(w):.2f}{unit}")
    return " ".join(out)


def build_read_payload(uid, channel, address, function, register, count):
    return {
        "growDirectorUid": uid,
        "type": "ModuleCommandNotification",
        "command": "ModbusRead",
        "channel": channel,
        "address": address,
        "function": function,
        "register": register,
        "count": count,
    }


def cmd_read(conn, args):
    p = build_read_payload(conn["uid"], args.channel, args.address, args.function,
                           args.register, args.count)
    res = probe_retry(conn, p, args.timeout, args.retries, args.print_cmd, args.show_pass)
    res["_register"] = args.register
    prefix = f"ch{args.channel} addr=0x{args.address:02X} reg=0x{args.register:04X} fn={args.function} n={args.count}"
    print(fmt_line(prefix, res, args.decode))
    return 0 if res["status"] == "ok" else 1


def cmd_read_addr(conn, args):
    p = build_read_payload(conn["uid"], args.channel, 0, 3, args.register, 1)
    res = probe_retry(conn, p, args.timeout, args.retries, args.print_cmd, args.show_pass)
    prefix = f"ch{args.channel} broadcast reg=0x{args.register:04X} (station address)"
    if res["status"] == "ok":
        dec = decode_read_frame(res["raw_io"]["data"])
        addr_byte = None
        if dec.get("ok"):
            raw = bytes.fromhex(res["raw_io"]["data"])
            addr_byte = raw[-3]
        print(f"{prefix} | OK  frame={res['raw_io']['data']} station_address=0x{addr_byte:02X}" if addr_byte is not None else f"{prefix} | OK  frame={res['raw_io']['data']}")
    else:
        print(fmt_line(prefix, res))
    return 0 if res["status"] == "ok" else 1


def cmd_write(conn, args):
    p = {
        "growDirectorUid": conn["uid"],
        "type": "ModuleCommandNotification",
        "command": "ModbusWrite",
        "channel": args.channel,
        "address": args.address,
        "function": args.function,
        "register": args.register,
        "data": args.data,
    }
    res = probe_retry(conn, p, args.timeout, args.retries, args.print_cmd, args.show_pass)
    prefix = f"ch{args.channel} addr=0x{args.address:02X} reg=0x{args.register:04X} fn={args.function} data={args.data}"
    cr = res.get("cmd_result") or {}
    if cr.get("status") == "accepted" or res["status"] == "ok":
        print(f"{prefix} | ACCEPTED (verify with a read)")
        return 0
    print(fmt_line(prefix, res))
    return 1


def cmd_serial(conn, args):
    p = {
        "growDirectorUid": conn["uid"],
        "type": "ModuleCommandNotification",
        "command": "SerialWrite",
        "channel": args.channel,
        "bytes": args.bytes,
        "expectResponse": not args.no_expect,
    }
    res = probe_retry(conn, p, args.timeout, args.retries, args.print_cmd, args.show_pass)
    prefix = f"ch{args.channel} serial bytes={args.bytes} expect={not args.no_expect}"
    if args.no_expect:
        cr = res.get("cmd_result") or {}
        print(f"{prefix} | {'ACCEPTED' if cr.get('status') == 'accepted' else 'SENT'}")
        return 0
    print(fmt_line(prefix, res))
    return 0 if res["status"] == "ok" else 1


def parse_channels(spec):
    return [int(x) for x in spec.split(",") if x.strip() != ""]


def cmd_scan_addr(conn, args):
    lo = parse_int(args.addr_from)
    hi = parse_int(args.addr_to)
    print(f"# scan channel {args.channel} reg=0x{args.register:04X} fn={args.function} addr 0x{lo:02X}..0x{hi:02X}")
    responders = []
    for addr in range(lo, hi + 1):
        p = build_read_payload(conn["uid"], args.channel, addr, args.function, args.register, args.count)
        res = probe_retry(conn, p, args.timeout, args.retries)
        res["_register"] = args.register
        prefix = f"  ch{args.channel} addr=0x{addr:02X}"
        print(fmt_line(prefix, res, args.decode))
        if res["status"] == "ok":
            responders.append(addr)
    print(f"# responders: {', '.join('0x%02X' % a for a in responders) if responders else 'none'}")
    return 0 if responders else 1


def cmd_scan_ports(conn, args):
    channels = parse_channels(args.channels)
    print(f"# scan ports {channels} addr=0x{args.address:02X} reg=0x{args.register:04X} fn={args.function}")
    hits = []
    for ch in channels:
        p = build_read_payload(conn["uid"], ch, args.address, args.function, args.register, args.count)
        res = probe_retry(conn, p, args.timeout, args.retries)
        res["_register"] = args.register
        prefix = f"  ch{ch} addr=0x{args.address:02X}"
        print(fmt_line(prefix, res, args.decode))
        if res["status"] == "ok":
            hits.append(ch)
    print(f"# responding channels: {', '.join(str(c) for c in hits) if hits else 'none'}")
    return 0 if hits else 1


def cmd_crc(conn, args):
    raw = bytes.fromhex(args.hex.replace(" ", ""))
    c = crc16(raw)
    print((args.hex.replace(" ", "").upper()) + c.hex().upper())
    return 0


def parse_int(s):
    return int(s, 0) if isinstance(s, str) else int(s)


def add_conn_args(pa):
    pa.add_argument("--uid", help="module UID (MAC), or env GD_UID")
    pa.add_argument("--broker", help="override broker (default: globals.h / GD_BROKER)")
    pa.add_argument("--port", help="override port")
    pa.add_argument("--user", help="override mqtt user")
    pa.add_argument("--password", help="override mqtt password")
    pa.add_argument("--globals", default="src/globals.h", help="path to globals.h for creds")
    pa.add_argument("--timeout", type=float, default=2.5, help="seconds to wait per probe")
    pa.add_argument("--retries", type=int, default=0, help="retry on timeout/no-response before declaring dead (0 = honest single probe)")
    pa.add_argument("--print-cmd", action="store_true", help="print equivalent mosquitto sub/pub commands")
    pa.add_argument("--show-pass", action="store_true", help="reveal password in --print-cmd")


def main():
    ap = argparse.ArgumentParser(prog="gd_sensor", description="Debug GrowDirector RS485 sensors over MQTT")
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("read", help="ModbusRead one register set (firmware builds CRC)")
    add_conn_args(r)
    r.add_argument("--channel", type=int, required=True)
    r.add_argument("--address", type=parse_int, required=True)
    r.add_argument("--register", type=parse_int, required=True)
    r.add_argument("--function", type=parse_int, default=3)
    r.add_argument("--count", type=parse_int, default=1)
    r.add_argument("--decode", choices=sorted(SENSOR_DECODERS), help="engineering decode of words")
    r.set_defaults(func=cmd_read)

    ra = sub.add_parser("read-addr", help="broadcast-read station address (address 0)")
    add_conn_args(ra)
    ra.add_argument("--channel", type=int, required=True)
    ra.add_argument("--register", type=parse_int, default=0x0001,
                    help="address register: 0x0001 for MP508 group, 0x0000 for XF-series")
    ra.set_defaults(func=cmd_read_addr)

    w = sub.add_parser("write", help="ModbusWrite (firmware builds CRC). CAUTION: changes device state")
    add_conn_args(w)
    w.add_argument("--channel", type=int, required=True)
    w.add_argument("--address", type=parse_int, required=True)
    w.add_argument("--register", type=parse_int, required=True)
    w.add_argument("--function", type=parse_int, default=16)
    w.add_argument("--data", required=True, help="hex bytes AFTER the register, no CRC")
    w.set_defaults(func=cmd_write)

    s = sub.add_parser("serial", help="SerialWrite literal frame (you supply full frame + CRC)")
    add_conn_args(s)
    s.add_argument("--channel", type=int, required=True)
    s.add_argument("--bytes", required=True, help="full hex frame including CRC")
    s.add_argument("--no-expect", action="store_true", help="do not wait for a response")
    s.set_defaults(func=cmd_serial)

    sa = sub.add_parser("scan-addr", help="sweep an address range on one channel")
    add_conn_args(sa)
    sa.add_argument("--channel", type=int, required=True)
    sa.add_argument("--from", dest="addr_from", required=True, help="start address (e.g. 0x70)")
    sa.add_argument("--to", dest="addr_to", required=True, help="end address (e.g. 0x73)")
    sa.add_argument("--register", type=parse_int, default=0x0009)
    sa.add_argument("--function", type=parse_int, default=3)
    sa.add_argument("--count", type=parse_int, default=1)
    sa.add_argument("--decode", choices=sorted(SENSOR_DECODERS))
    sa.set_defaults(func=cmd_scan_addr)

    sp = sub.add_parser("scan-ports", help="probe one address/register across channels")
    add_conn_args(sp)
    sp.add_argument("--channels", default="0,1,2,3")
    sp.add_argument("--address", type=parse_int, required=True)
    sp.add_argument("--register", type=parse_int, default=0x0009)
    sp.add_argument("--function", type=parse_int, default=3)
    sp.add_argument("--count", type=parse_int, default=1)
    sp.add_argument("--decode", choices=sorted(SENSOR_DECODERS))
    sp.set_defaults(func=cmd_scan_ports)

    c = sub.add_parser("crc", help="append CRC16-Modbus to a hex frame (offline helper)")
    c.add_argument("hex", help="hex bytes without CRC")
    c.set_defaults(func=lambda conn, a: cmd_crc(conn, a), needs_conn=False)

    args = ap.parse_args()
    if getattr(args, "needs_conn", True) is False:
        sys.exit(args.func(None, args))
    conn = resolve_conn(args)
    sys.exit(args.func(conn, args))


if __name__ == "__main__":
    main()
