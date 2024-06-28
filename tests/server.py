#!/usr/bin/env python3
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


def tone(samples, frequency):
    amplitude = 5
    twoPiF = 2 * np.pi * frequency
    buffer = amplitude * np.sin(twoPiF * samples)
    buffer += np.random.normal(0, 10, len(samples))
    return buffer

@app.route("/")
def index_handler():
    return send_from_directory(static_directory, "index.html")

@app.route("/measurements", methods=["GET"])
def handle_measurement_request():
    now = int(time.time() * 1000)
    times = np.array([now + 20 * i for i in range(10)])
    response = json.dumps(
        {"ms": times.tolist(), "preassure": tone(times / 1000, 0.8).tolist()}
    )
    return response


@app.route("/downloads", methods=["GET"])
def handle_downloads_request():
    response = json.dumps(
        {
            "download-links": [
                "/test/file_example_WAV_1MG.wav",
                "/test/file_example_WAV_2MG.wav",
            ],
            "link-texts": ["file_example_WAV_1MG.wav", "file_example_WAV_2MG.wav"],
        }
    )
    return response


@app.route("/set_wifi", methods=["POST"])
def handle_wifi_form():
    # Access form data using request.form dictionary
    print(request.form)
    ssid = request.form.get("ssid")
    password = request.form.get("password")
    print("SSID:", ssid, "password", password)
    # TODO fix this!

    # Process the extracted data (SSID and password) here
    # ... your logic to handle SSID and password ...

    return f"I received ssid={ssid} and password={password}"


if __name__ == "__main__":
    app.run(debug=True, port=8000)
