var pffft_module = null;
var measurementAnalyzer = null;

pffft().then(function(Module) {
  console.log("PFFFT Module initialized");
  pffft_module = Module;
});

function showDownloadOptions(downloadOptions) {
  downloadList = document.getElementById("download-list");
  for (let i = 0; i < downloadOptions["files"].length; i++) {
    const labelDiv = document.createElement("div");
    const buttonGroupDiv = document.createElement("div");
    const downloadButton = document.createElement("button");
    const analyzeButton = document.createElement("button");
    const fileName = downloadOptions["files"][i];
    labelDiv.textContent = fileName;
    downloadButton.classList.add("btn");
    analyzeButton.classList.add("btn");
    downloadButton.classList.add("btn-secondary");
    analyzeButton.classList.add("btn-primary");
    buttonGroupDiv.classList.add("btn-group");
    buttonGroupDiv.setAttribute("role", "group");
    buttonGroupDiv.setAttribute("aria-label", "Button Group");
    buttonGroupDiv.appendChild(downloadButton);
    buttonGroupDiv.appendChild(analyzeButton);
    const listDiv = document.createElement("div");
    const endpoint = "/download?&file=" + downloadOptions["files"][i];
    downloadButton.textContent = "Download";
    downloadButton.addEventListener("click", (event) => {
      event.preventDefault();
      console.log("Downloading", endpoint);
      downloadFile(endpoint);
    });

    analyzeButton.addEventListener("click", (event) => {
      event.preventDefault();
      console.log("Downloading", endpoint, "for analysis");
      downloadAndAnalyze(endpoint);
    });
    analyzeButton.textContent = "Analyze";
    listDiv.classList.add("list-group-item");
    listDiv.classList.add("list-group-item-action");
    listDiv.classList.add("d-flex");
    listDiv.classList.add("justify-content-between");
    listDiv.classList.add("align-items-center");
    listDiv.appendChild(labelDiv);
    listDiv.appendChild(buttonGroupDiv);
    downloadList.appendChild(listDiv);
  }
}


// Make sure the measurements are stopped
const stopMeasurementRequest = new XMLHttpRequest();
stopMeasurementRequest.onreadystatechange = function() {
  if (this.readyState == 4 && this.status == 200) {
    console.log("Stopped measurements");
  }
};
stopMeasurementRequest.open("GET", "/stop_measurements");
stopMeasurementRequest.send();


// Get the list of measurement files
const downloadRequest = new XMLHttpRequest();
downloadRequest.onreadystatechange = function() {
  if (this.readyState == 4 && this.status == 200) {
    var downloadOptions = JSON.parse(this.responseText);
    showDownloadOptions(downloadOptions);
  }
};
downloadRequest.open("GET", "/downloads");
downloadRequest.send();

function downloadFile(endpoint) {
  window.open(endpoint);
}

function downloadAndAnalyze(endpoint) {

  setProgressbar(0, "Downloading");
  fetch(endpoint)
    .then(response =>
      processChunkedResponse(response))
    .catch(error => console.error("Error:", error));
}

async function processChunkedResponse(response) {
  let bytesRead = 0;

  let reader = response.body.getReader();
  const totalSize = +response.headers.get("Content-Length");

  rawData = []
  while (true) {
    const { done, value } = await reader.read();

    if (done) {
      break;
    }
    rawData.push(value);
    bytesRead += value.length;
    const downloadProgress = Math.round(100 * bytesRead / totalSize);
    setProgressbar(downloadProgress, "Downloading");
  }
  let downloadedData = new Uint8Array(bytesRead);
  let position = 0;
  for (let chunk of rawData) {
    downloadedData.set(chunk, position);
    position += chunk.length;
  }
  // Parse measurements in downloaded data
  setProgressbar(0, "Parsing Data");
  if (downloadedData.length < 8) {
    console.error("File too small - doesnt contain a header");
    setProgressbar(0, "No work pending");
    return;
  }
  let offset = 0;
  const header = String.fromCharCode.apply(null, downloadedData.slice(0, 4));
  offset += 4;
  if (header != "data") {
    console.error("File header was", header, " - I only accept 'data'");
    setProgressbar(0, "No work pending");
    return;
  }
  const bytesToRead = new Uint32Array(downloadedData.slice(offset, offset + 4))[0];
  offset += 4;
  if (bytesToRead % 4 != 0) {
    console.error("The given file size is not a multiple of 4 - should be with float values!", bytesToRead);
    setProgressbar(0, "No work pending");
    return;
  }
  const measurementData = new Float32Array(downloadedData.slice(8, downloadedData.length));
  setProgressbar(100, "Parsing Data");

  console.log("Download finished... starting analysis");
  setProgressbar(0, "No work pending");
  // Create measurement Analyzer
  measurementAnalyzer = new measurementAnalyzer(measurementData);
}

function setProgressbar(value, label) {
  const progressbar = document.getElementById("progressbar");
  const progressbarLabel = document.getElementById("progressbarLabel");
  progressbarLabel.textContent = label;
  progressbar.style.width = `${value}%`;
  progressbar.setAttribute("aria-valuenow", value);
  progressbar.textContent = `${Math.round(value)}%`;
}

class MeasurementAnalyzer {
  constructor(time_sequence) {
    this.sequence = time_sequence;
    this.fft_window_size = 1024;
    this.startIdx = 0;
    this.endIdx = time_sequence.length;
    this.spectrogram = new Spectrogram(this.fft_window_size, width); // TODO get width from canvas
    // Run intial analysis
    this.analyze(this.startIdx, this.endIdx);
  }

  analyze() {
    this.analyze(this.startIdx, this.endIdx);
  }

  async analyze(startIdx, endIdx) {
    const samplesToAnalyze = Math.max(endIdx - startIdx, 0);
    const stride = Math.floor(samplesToAnalyze / this.spectrogram_width);
    // center the fft window at each selected sapmle and compute a spectrum
    // TODO embarisingly parallel -> multithread?
    for (let i = 0; i < this.spectrogram_width; i++) {
      // show progress
      const progress = 100 * i / this.spectrogram.width;
      setProgressbar(progress, "Computing Spectra");
      // index of center measurement
      const sample_idx = startIdx + i * stride;
      // span window around that center
      let window_start_idx = sample_idx - this.fft_window_size / 2;
      let window_end_idx = sample_idx + this.fft_window_size / 2;
      // check if padding is necessary at the start or end of the window
      var start_padding_size = 0;
      var end_padding_size = 0;
      if (window_start_idx < 0) {
        start_padding_size = -window_start_idx;
        window_start_idx = 0;
      }
      if (window_end_idx > this.sequence.length) {
        end_padding_size = window_end_idx - this.sequence.length;
        window_end_idx = this.sequence.length;
      }

      // Slice the corresponding values from the sequence
      var fft_time_sequence = null;
      if (start_padding_size == 0 && end_padding_size == 0) {
        fft_time_sequence = this.sequence.slice(window_start_idx, window_end_idx);
      } else {
        // con the edges of the measurements create zero padding
        const start_padding = new Float32Array(start_padding_size);
        const end_padding = new Float32Array(end_padding_size);
        const actual_values = this.sequence.slice(window_start_idx, window_end_idx);

        fft_time_sequence = new Float32Array(start_padding_size + end_padding_size + actual_values.length);
        fft_time_sequence.set(start_padding);
        fft_time_sequence.set(actual_values, start_padding_size);
        fft_time_sequence.set(end_padding, start_padding_size + actual_values.length);
      }


      const spectrum = fourier_transform(fft_time_sequence);
      this.spectrogram.setSpectrum(i, spectrum);

    }
    this.startIdx = startIdx;
    this.endIdx = endIdx;

    setProgressbar(0, "No work pending");
  }

  setFFTWindowSize(fft_window_size) {
    this.fft_window_size = fft_window_size;
    this.spectrogram.setFFTWindowSize(fft_window_size);
    this.analyze();
  }
}

class Spectrogram {
  constructor(fft_window_size, width) {
    this.width = width;
    this.total_frequency_steps = fft_window_size / 2;
    this.frequencies_without_constant = this.total_frequency_steps - 1;

    // create webgl texture for holding the ata
    const canvas = document.getElementById("webglCanvas");
    this.gl = canvas.getContext("webgl2");
    this.texture = this.gl.createTexture();
    this.gl.texParameteri(
      this.gl.TEXTURE_2D,
      this.gl.TEXTURE_MIN_FILTER,
      this.gl.NEAREST,
    )
    this.gl.texParameteri(
      this.gl.TEXTURE_2D,
      this.gl.TEXTURE_MAG_FILTER,
      this.gl.NEAREST,
    );
    this.gl.texParameteri(
      this.gl.TEXTURE_2D,
      this.gl.TEXTURE_WRAP_S,
      this.gl.CLAMP_TO_EDGE,
    );
    this.gl.texParameteri(
      this.gl.TEXTURE_2D,
      this.gl.TEXTURE_WRAP_T,
      this.gl.CLAMP_TO_EDGE,
    );
    // initialize the texture with zeros
    this.allocateTexture();
    this.columnMax = new Float32Array(width).fill(Number.NEGATIVE_INFINITY);
    this.columnMin = new Float32Array(width).fill(Number.POSITIVE_INFINITY);
    this.columnMaxIdx = new Int32Array(width);
    this.columnMinIdx = new Int32Array(width);
    this.max = Number.MIN_VALUE;
    this.min = Number.MAX_VALUE;
  }

  setSpectrum(columnIdx, spectrum) {
    if (spectrum.length != this.total_frequency_steps) {
      console.error("I expected the spectrum to have", this.total_frequency_steps, "entries, not", spectrum.length);
      return;
    }
    if (columnIdx >= this.width) {
      console.error("The column index ist out of range: ", columnIdx, "this.width:", this.width);
      return;
    }
    // Upload spectrum to texture
    this.gl.bindTexture(this.gl.TEXTURE_2D, this.texture);
    this.gl.texSubImage2D(
      this.gl.TEXTURE_2D,
      0, // level
      columnIdx, // x-offset
      0, // y-offset
      1, // width of uploaded column
      this.frequencies_without_constant, // height of column
      this.gl.RED, // format
      this.gl.FLOAT, // type
      spectrum.slice(1), // data
    );
  }

  setFFTWindowSize(fft_window_size) {
    this.total_frequency_steps = fft_window_size / 2;
    this.frequencies_without_constant = this.total_frequency_steps - 1;
    this.allocateTexture();
  }

  allocateTexture() {
    // Resize texture
    this.gl.bindTexture(this.gl.TEXTURE_2D, this.texture);
    const data = new Float32Array(this.width * this.frequencies_without_constant);
    this.gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.R32F,
      this.width,
      this.height,
      0,
      gl.RED,
      gl.FLOAT,
      data,
    );
  }

  setWidth(newWidth) {
    if (this.width == new width) {
      return;
    }
    this.width = newWidth;
    this.allocateTexture();
    // Reset the min and max values and indices
    this.columnMax = new Float32Array(this.width).fill(Number.NEGATIVE_INFINITY);
    this.columnMin = new Float32Array(this.width).fill(Number.POSITIVE_INFINITY);
    this.columnMaxIdx = new Int32Array(this.width);
    this.columnMinIdx = new Int32Array(this.width);
    this.max = Number.MIN_VALUE;
    this.min = Number.MAX_VALUE;
    this.maxIdx = -1;
    this.minIdx = -1;
  }
}

// PFFFT STUFF
let pffft_runner = null;
let dataPtr = null;
let dataHeap = null;
function cleanup_pffft() {
  pffft_module._free(dataPtr);
  pffft_module._pffft_runner_destroy(pffft_runner);
  dataPtr = null;
  pffft_runner = null;
}

function initialize_pffft(fft_window) {
  if (pffft_runner) {
    return;
  }
  const audio_block_size = fft_window;
  const bytes_per_element = 4;
  const nDataBytes = audio_block_size * bytes_per_element;

  pffft_runner = pffft_module._pffft_runner_new(
    audio_block_size,
    bytes_per_element,
  );

  dataPtr = pffft_module._malloc(nDataBytes);
  dataHeap = new Uint8Array(pffft_module.HEAPU8.buffer, dataPtr, nDataBytes);
}

function fourier_transform(buffer) {
  // Copy data to Emscripten heap (directly accessed from Module.HEAPU8)
  dataHeap.set(new Uint8Array(buffer.buffer));

  // Call function and get result
  pffft_module._pffft_runner_transform_magnitudes(
    pffft_runner,
    dataHeap.byteOffset,
  );

  let fft_squared_magnitudes = new Float32Array(
    dataHeap.buffer,
    dataHeap.byteOffset,
    timeSequence.length,
  );
  fft_squared_magnitudes = fft_squared_magnitudes.slice(
    0,
    fft_squared_magnitudes.length / 2,
  );

  const scaled_magnitudes = fft_squared_magnitudes.map(
    (value) => (2 * value ** 0.5) / timeSequence.length,
  );

  return scaled_magnitudes;
}
