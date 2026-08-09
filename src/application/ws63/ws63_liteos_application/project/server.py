import json
import os
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


TEAMMATE_NGROK = os.environ.get("TEAMMATE_NGROK", "").rstrip("/")


class ScanProxyHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print("%s - %s" % (self.client_address[0], fmt % args))

    def _send_json(self, status, data):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/scan":
            self._send_json(404, {"result": "error", "message": "not found"})
            return

        content_length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(content_length)

        try:
            data = json.loads(body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            self._send_json(400, {"result": "error", "message": str(exc)})
            return

        print("Local scan:", data)

        if not TEAMMATE_NGROK:
            self._send_json(200, {"status": "APPROVED", "led": "GREEN"})
            return

        forward_body = json.dumps(data).encode("utf-8")
        forward_request = Request(
            TEAMMATE_NGROK + "/scan",
            data=forward_body,
            headers={
                "Content-Type": "application/json",
                "ngrok-skip-browser-warning": "true",
            },
            method="POST",
        )

        try:
            with urlopen(forward_request, timeout=10) as response:
                response_body = response.read()
                content_type = response.headers.get("Content-Type", "application/json")
                self.send_response(response.status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(response_body)))
                self.end_headers()
                self.wfile.write(response_body)
        except HTTPError as exc:
            error_body = exc.read()
            content_type = exc.headers.get("Content-Type", "application/json")
            self.send_response(exc.code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(error_body)))
            self.end_headers()
            self.wfile.write(error_body)
        except URLError as exc:
            self._send_json(502, {"result": "error", "message": str(exc.reason)})


if __name__ == "__main__":
    server = HTTPServer(("0.0.0.0", 5000), ScanProxyHandler)
    print("Scan proxy listening on http://0.0.0.0:5000")
    print("Forward target:", TEAMMATE_NGROK or "(none, local GREEN test response)")
    server.serve_forever()
