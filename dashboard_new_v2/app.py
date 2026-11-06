import json
import os
import queue
import threading
import time
from datetime import datetime, timezone

import serial
from serial.tools import list_ports
from flask import Flask, Response, jsonify, render_template, request

app = Flask(__name__, template_folder="templates")
events = queue.Queue(maxsize=500)
state_lock = threading.Lock()
state = {
    "connected": False,
    "port": os.getenv("SERIAL_PORT", "/dev/cu.usbserial-0001"),
    "baud": int(os.getenv("SERIAL_BAUD", "115200")),
    "error": "",
    "last_line": "",
    "total_hits": 0,
    "last_hit": None,
}
serial_handle = None
reader_thread = None
stop_event = threading.Event()


def publish_event(event):
    try:
        events.put_nowait(event)
    except queue.Full:
        try:
            events.get_nowait()
        except queue.Empty:
            pass
        try:
            events.put_nowait(event)
        except queue.Full:
            pass


def serial_reader(port, baud):
    global serial_handle
    try:
        ser = serial.Serial(port, baud, timeout=1)
        # Opening a USB serial port may reset ESP32. Ignore boot text until JSON arrives.
        time.sleep(1.8)
        with state_lock:
            state["connected"] = True
            state["error"] = ""
            state["port"] = port
            state["baud"] = baud
        serial_handle = ser

        while not stop_event.is_set():
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue

            with state_lock:
                state["last_line"] = line

            # Firmware prints human-readable startup lines prefixed with #.
            # Boot ROM garbage and incomplete lines are intentionally ignored.
            try:
                payload = json.loads(line)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue

            if payload.get("event") != "HIT":
                continue

            payload["received_at"] = datetime.now().astimezone().strftime("%H:%M:%S")
            with state_lock:
                state["total_hits"] += 1
                state["last_hit"] = payload
            publish_event(payload)

    except Exception as exc:
        with state_lock:
            state["connected"] = False
            state["error"] = str(exc)
    finally:
        if serial_handle is not None:
            try:
                serial_handle.close()
            except Exception:
                pass
        serial_handle = None
        with state_lock:
            state["connected"] = False


@app.get("/")
def index():
    return render_template("index.html")


@app.get("/api/status")
def api_status():
    with state_lock:
        return jsonify(dict(state))


@app.get("/api/ports")
def api_ports():
    return jsonify([{"device": p.device, "description": p.description} for p in list_ports.comports()])


@app.post("/api/connect")
def api_connect():
    global reader_thread
    body = request.get_json(silent=True) or {}
    port = str(body.get("port", state["port"])).strip()
    try:
        baud = int(body.get("baud", state["baud"]))
    except (TypeError, ValueError):
        return jsonify({"ok": False, "error": "Baud rate must be a number"}), 400
    if not port:
        return jsonify({"ok": False, "error": "Please choose a serial port"}), 400
    if baud not in (9600, 19200, 38400, 57600, 115200, 230400):
        return jsonify({"ok": False, "error": "Unsupported baud rate"}), 400

    with state_lock:
        if state["connected"]:
            return jsonify({"ok": False, "error": "Already connected. Restart the dashboard to change port."}), 409
        state["port"] = port
        state["baud"] = baud
        state["error"] = ""

    stop_event.clear()
    reader_thread = threading.Thread(target=serial_reader, args=(port, baud), daemon=True)
    reader_thread.start()
    return jsonify({"ok": True, "message": "Connecting to serial port"})


@app.get("/api/events")
def api_events():
    def stream():
        # Send a connection comment so browsers know the stream is alive.
        yield ": connected\n\n"
        while True:
            try:
                item = events.get(timeout=15)
                yield "data: " + json.dumps(item, ensure_ascii=False) + "\n\n"
            except queue.Empty:
                yield ": keep-alive\n\n"
    return Response(stream(), mimetype="text/event-stream",
                    headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"})


if __name__ == "__main__":
    # Start with a connection attempt using SERIAL_PORT, if provided.
    default_port = os.getenv("SERIAL_PORT", "/dev/cu.usbserial-0001")
    threading.Thread(target=serial_reader, args=(default_port, 115200), daemon=True).start()
    app.run(host="127.0.0.1", port=5001, debug=False, threaded=True)
