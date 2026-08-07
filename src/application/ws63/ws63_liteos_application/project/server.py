import json
import os
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from flask import Flask, request


app = Flask(__name__)

TEAMMATE_NGROK = os.environ.get("TEAMMATE_NGROK", "").rstrip("/")


@app.route("/scan", methods=["POST"])
def scan():
    data = request.get_json(force=True)
    print("Local scan:", data)

    if not TEAMMATE_NGROK:
        return {"result": "ok", "message": "TEAMMATE_NGROK not set"}

    body = json.dumps(data).encode("utf-8")
    forward_request = Request(
        TEAMMATE_NGROK + "/scan",
        data=body,
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
            return response_body, response.status, {"Content-Type": content_type}
    except HTTPError as exc:
        error_body = exc.read()
        content_type = exc.headers.get("Content-Type", "application/json")
        return error_body, exc.code, {"Content-Type": content_type}
    except URLError as exc:
        return {"result": "error", "message": str(exc.reason)}, 502


app.run(
    host="0.0.0.0",
    port=5000
)
