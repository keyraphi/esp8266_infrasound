import numpy as np
import os


def tone(samples, frequency):
    samples = np.arange(samples) / 50
    amplitude = 5
    twoPiF = 2 * np.pi * frequency
    buffer = amplitude * np.sin(twoPiF * samples)
    buffer += np.random.normal(0, 2, len(samples))
    return buffer

def write_data(raw_file, data):
    raw_file.write(b"data")
    raw_file.write(np.uint32(len(data) * 4))
    raw_file.write(data.astype(np.float32).tobytes())

def main():
    data = tone(100000, 2)
    data = data / np.max(data)
    with open("test.raw", "wb") as raw_file:
        write_data(raw_file, data)

if __name__ == "__main__":
    main()
