#!/usr/bin/env python3
import json

from http.server import SimpleHTTPRequestHandler, HTTPServer
import time
import os
import numpy as np

static_directory = os.path.abspath("../static")

def tone(samples, frequency):
    amplitude = 5
    twoPiF = 2 * np.pi * frequency
    buffer = amplitude * np.sin(twoPiF * samples)
    buffer += np.random.normal(0, 10, len(samples))
    return buffer

class WebRequestHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/measurements":
            now = int(time.time()*1000)
            times = np.array([now + 20 * i for i in range(10)])
            response = json.dumps(
                {
                    "ms": times.tolist() ,
                    "preassure": tone(times/1000, 0.8).tolist()
                }
            )
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(response.encode("utf-8"))
        elif self.path == "/downloads":
            response = json.dumps(
                {
                "download-links": ["/test/file_example_WAV_1MG.wav", "/test/file_example_WAV_2MG.wav"],
                "link-texts": ["file_example_WAV_1MG.wav","file_example_WAV_2MG.wav"]
                }
            )
            print(response)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(response.encode("utf-8"))
        else:
            super().do_GET()


if __name__ == "__main__":
    print(static_directory)
    server = HTTPServer(("localhost", 8000), lambda *_: WebRequestHandler(*_, directory=static_directory))
    server.serve_forever()
