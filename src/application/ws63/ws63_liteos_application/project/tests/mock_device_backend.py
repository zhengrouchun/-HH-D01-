"""Local protocol fixture for bench testing. This is not the production backend."""

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


ACCESS_CODES = {
    "S1": "PROD-7f2a", "S2": "FDA-91xq", "S3": "WARE-3kd8",
    "S4": "PUB-c72m", "S5": "PRIV-a9z1", "CP": "VERIFY-q4m8",
}


def serve(host: str, port: int, batch_id: str, expected: int) -> None:
    state = {
        "ok": True, "state_version": 1, "mode": "NONE", "phase": "IDLE",
        "batch_id": batch_id, "tags_read": 0, "tags_expected": expected,
        "progress_percent": 0, "screen_status": "NONE", "color": None,
        "risk_percent": 0, "message": "Select a mode", "requested_view": "NONE",
    }
    unique = set()

    class Handler(BaseHTTPRequestHandler):
        def reply(self, code, obj):
            data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path == "/device/state":
                self.reply(200, state)
            else:
                self.reply(404, {"ok": False, "error": "UNKNOWN_PATH"})

        def do_POST(self):
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length < 0 or length > 16384:
                    raise ValueError("invalid body length")
                payload = json.loads(self.rfile.read(length))
                if not isinstance(payload, dict):
                    raise ValueError("body must be an object")
            except (ValueError, UnicodeDecodeError, json.JSONDecodeError) as exc:
                self.reply(400, {"ok": False, "error": str(exc)})
                return

            if self.path == "/device/command":
                mode = payload.get("mode")
                if (payload.get("command") != "SELECT_MODE" or
                        payload.get("source") != "DEVICE" or mode not in ACCESS_CODES):
                    self.reply(400, {"ok": False, "error": "BAD_COMMAND"})
                elif payload.get("access_code") != ACCESS_CODES[mode]:
                    self.reply(403, {"ok": False, "error": "BAD_CODE"})
                else:
                    unique.clear()
                    state.update(mode=mode, phase="READY", tags_read=0,
                                 progress_percent=0, screen_status="NONE",
                                 color=None, risk_percent=0,
                                 message=f"{mode} selected")
                    state["state_version"] += 1
                    self.reply(200, state)
                return

            if self.path == "/device/scan":
                readings = payload.get("readings")
                if payload.get("mode") != state["mode"] or payload.get("batch_id") != batch_id:
                    self.reply(409, {"ok": False, "error": "MODE_MISMATCH"})
                elif payload.get("access_code") != ACCESS_CODES.get(state["mode"]):
                    self.reply(403, {"ok": False, "error": "BAD_CODE"})
                elif state["phase"] not in ("READY", "SCANNING"):
                    self.reply(409, {"ok": False, "error": "WRONG_PHASE"})
                elif not isinstance(readings, list) or not readings or not all(
                    isinstance(x, dict) and isinstance(x.get("chip_uid"), str) and
                    isinstance(x.get("rssi_dbm"), int) for x in readings
                ):
                    self.reply(400, {"ok": False, "error": "BAD_READINGS"})
                else:
                    for reading in readings:
                        unique.add(reading["chip_uid"])
                    state["tags_read"] = len(unique)
                    state["progress_percent"] = min(100, len(unique) * 100 // expected) if expected else None
                    state["phase"] = "DONE" if payload.get("final") is True else "SCANNING"
                    state["screen_status"] = "APPROVED" if state["phase"] == "DONE" else "NONE"
                    state["color"] = "GREEN" if state["phase"] == "DONE" else None
                    state["message"] = "Fixture result only" if state["phase"] == "DONE" else "Scanning"
                    state["state_version"] += 1
                    self.reply(200, state)
                return

            self.reply(404, {"ok": False, "error": "UNKNOWN_PATH"})

    server = ThreadingHTTPServer((host, port), Handler)
    print(f"Mock only: http://{host}:{port} batch_id={batch_id} expected={expected}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--batch-id", default="BATCH-TEST-001")
    parser.add_argument("--expected", type=int, default=3)
    args = parser.parse_args()
    serve(args.host, args.port, args.batch_id, args.expected)
