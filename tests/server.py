#!/usr/bin/env python3
from multiprocessing import Process, Manager
import json
from flask import Flask, request, send_from_directory

import time
import os
import numpy as np

static_directory = os.path.abspath("../static")

app = Flask(
    __name__,
    static_url_path="",
    static_folder=static_directory,
)



def get_measurement(time, frequency, amplitude=5, noise_sigma=1):
    twoPiF = 2 * np.pi * frequency
    preassure = amplitude * np.sin(twoPiF * time)
    preassure += np.random.randn() * noise_sigma
    return preassure


@app.route("/")
def index_handler():
    return send_from_directory(static_directory, "index.html")


@app.route("/measurements", methods=["GET"])
def handle_measurement_request():
    start_with_idx = int(request.args.get("start_with_idx"))
    max_length = int(request.args.get("max_length"))
    response_data = sensor_data[start_with_idx:]
    response_data = response_data[-max_length:]
    print("DEBUG: len(response_data)", len(response_data))
    next_start_idx = start_with_idx + len(response_data)
    times = [d[0] for d in response_data]
    preassure = [d[1] for d in response_data]
    print("next_start_idx", next_start_idx)
    response = json.dumps(
        {"next_start_idx": next_start_idx, "ms": times, "preassure": preassure}
    )
    return response


@app.route("/downloads", methods=["GET"])
def handle_downloads_request():
    response = json.dumps(
        {
            "files": [
                "0",
                "0_1",
                "1235467876",
            ],
        }
    )
    return response

@app.route("/download", methods=["GET"])
def handle_download_request():
    filename = int(request.args.get("file"))
    print("file:", filename, "was requested")
    return ("", 204)

@app.route("/set_wifi", methods=["POST"])
def handle_wifi_form():
    # Access form data using request.form dictionary
    ssid = request.form.get("ssid")
    password = request.form.get("password")
    print("SSID:", ssid, "password", password)
    return ("", 204)

sensor_data = None

def pollSensor(shared_list):
    while True:
        t = time.time()
        v = get_measurement(t, 0.5, amplitude=0.2)  # This is the frequency
        shared_list.append((t, v))
        time.sleep(0.02)


if __name__ == "__main__":
    with Manager() as manager:
        sensor_data = manager.list()
        sensor_reader = Process(
            target=pollSensor, name="SensorReadingProcess", args=(sensor_data,)
        )
        sensor_reader.start()
        print("Running server")
        app.run(debug=True, port=8000)
        sensor_reader.join()
