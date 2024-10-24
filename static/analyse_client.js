var pffft_module = null;
var measurementAnalyzer = null;

pffft().then(function(Module) {
  console.log("PFFFT Module initialized");
  pffft_module = Module;
});

const chartSoundPressureOverTime = new Highcharts.Chart({
  chart: {
    renderTo: "soundpressure-over-time",
    animation: false,
  },
  title: { text: "Lautstärke über Dauer der Messung" },
  series: [
    {
      showInLegend: false,
      data: [],
    },
  ],
  plotOptions: {
    line: {
      dataLabels: { enabled: false },
    },
    series: {
      color: "#059e8a",
    },
  },
  xAxis: {
    type: "linear",
    title: { text: "seconds" },
    gridLineWidth: 1,
  },
  yAxis: {
    title: { text: "Schallpegel [dB(G)]" },
    gridLineWidth: 1,
  },
  credits: { enabled: false },
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
  measurementAnalyzer = new MeasurementAnalyzer(measurementData);
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
    if (pffft_runner) {
      cleanup_pffft();
    }
    initialize_pffft(this.fft_window_size);

    this.startIdx = 0;
    this.endIdx = time_sequence.length;
    this.spectrogram = new Spectrogram(this.fft_window_size); // TODO get width from canvas
    // TODO make sure the number is correct
    this.durationSeconds = linspace(0, time_sequence.length * 20, Math.floor(time_sequence.length / this.spectrogram.width));
    this.totalSoundPressureLevels = new Float32Array(this.durationSeconds.length);
    // Run intial analysis
    this.analyze(this.startIdx, this.endIdx);
  }

  analyze(startIdx, endIdx) {
    if (typeof startIdx == "undefined") {startIdx = this.startIdx;}
    if (typeof endIdx == "undefined") {endIdx = this.endIdx;}
    const samplesToAnalyze = Math.max(endIdx - startIdx, 0);
    const stride = Math.floor(samplesToAnalyze / this.spectrogram.width);
    if (stride == 0) {
      stride = 1;
    }
    // center the fft window at each selected sapmle and compute a spectrum
    // TODO embarisingly parallel -> multithread?
    for (let i = 0; i < this.spectrogram.width; i += stride) {
      // show progress
      const progress = 100 * i / this.spectrogram.width;
      setProgressbar(progress, "Anayzing");
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
      // update spectrogram
      this.spectrogram.setSpectrum(i, spectrum);
      // set sound pressure level value in chart
      console.log("DEBUG: fft_time_sequence", fft_time_sequence);
      console.log("DEBUG: spectrum", spectrum);
      this.setSoundpressure(i, spectrum);
    }
    this.startIdx = startIdx;
    this.endIdx = endIdx;

    setProgressbar(0, "No work pending");
  }

  setSoundpressure(index, spectrum) {
    const frequencies = linspace(0, 25, spectrum.length);
    const totalSoundPressureLevel = computeTotalDBG(frequencies, spectrum);
    console.log("DEBUG: totalSoundPressureLevel", totalSoundPressureLevel);
    this.totalSoundPressureLevels[index] = totalSoundPressureLevel;

    // TODO don't always do this it might be quite slow
    const chart_data = [];
    for (let i=0; i < this.durationSeconds.length; i++) {
      chart_data.push([this.durationSeconds[i], this.totalSoundPressureLevels[i]]);
    }
    chartSoundPressureOverTime.series[0].setData(chart_data, false, false, false);
    chartSoundPressureOverTime.update({}, true, false, false);
  }

  setFFTWindowSize(fft_window_size) {
    // Get spectrum for the selected samples
    this.fft_window_size = fft_window_size;
    this.spectrogram.setFFTWindowSize(fft_window_size);
    cleanup_pffft();
    initialize_pffft(this.fft_window_size);
    this.analyze();
  }
}

class Spectrogram {
  constructor(fft_window_size) {
    this.width = document.getElementById("SpectrogramContainer").offsetWidth;
    this.total_frequency_steps = fft_window_size / 2;

    // create webgl texture for holding the ata
    const canvas = document.getElementById("webglCanvas");
    this.gl = canvas.getContext("webgl2");
    this.texture = this.gl.createTexture();
    this.gl.bindTexture(this.gl.TEXTURE_2D, this.texture);
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
    this.columnMax = new Float32Array(this.width).fill(Number.NEGATIVE_INFINITY);
    this.columnMin = new Float32Array(this.width).fill(Number.POSITIVE_INFINITY);
    this.columnMaxIdx = new Int32Array(this.width);
    this.columnMinIdx = new Int32Array(this.width);
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
      this.total_frequency_steps, // height of column
      this.gl.RED, // format
      this.gl.FLOAT, // type
      spectrum, // data
    );
  }

  setFFTWindowSize(fft_window_size) {
    this.total_frequency_steps = fft_window_size / 2;
    this.allocateTexture();
  }

  allocateTexture() {
    // allocating memory for texture
    console.log("Allocating texture of size", this.width, this.total_frequency_steps);
    this.gl.bindTexture(this.gl.TEXTURE_2D, this.texture);
    const data = new Float32Array(this.width * this.total_frequency_steps);
    this.gl.texImage2D(
      this.gl.TEXTURE_2D,
      0,
      this.gl.R32F,
      this.width,
      this.height,
      0,
      this.gl.RED,
      this.gl.FLOAT,
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

// utilities
function linspace(start, stop, num) {
  const result = new Float32Array(num);
  result[0] = start;
  if (num === 1) {
    return result;
  }
  result[result.length - 1] = stop;

  step = (stop - start) / (num - 1);
  for (let i = 1; i < num - 1; ++i) {
    result[i] = start + step * i;
  }
  return result;
}

// Computing noise level
function computeSPLSpectrum(frequencies, spectrum) {
  const sqrt_2 = Math.sqrt(2);
  const p_ref = 20e-6; // Reference value for SPL 20 micro pascal
  return spectrum.map((value) => 20 * Math.log10(value / sqrt_2 / p_ref));
}

const gWeightingTable = [
  { f: 0.25, gValue: -88 },
  { f: 0.315, gValue: -80 },
  { f: 0.4, gValue: -72.1 },
  { f: 0.5, gValue: -64.3 },
  { f: 0.63, gValue: -56.6 },
  { f: 0.8, gValue: -49.5 },
  { f: 1, gValue: -43 },
  { f: 1.25, gValue: -37.5 },
  { f: 1.6, gValue: -32.6 },
  { f: 2.0, gValue: -28.3 },
  { f: 2.5, gValue: -24.1 },
  { f: 3.15, gValue: -20 },
  { f: 4, gValue: -16 },
  { f: 5, gValue: -12 },
  { f: 6.3, gValue: -8 },
  { f: 8, gValue: -4 },
  { f: 10, gValue: 0 },
  { f: 12.5, gValue: 4 },
  { f: 16, gValue: 7.7 },
  { f: 20, gValue: 9.0 },
  { f: 25, gValue: 3.7 },
];
/**
 * Function to compute the G-weighting for an arbitrary frequency using linear interpolation
 * @param {number} f - The frequency for which to compute the G-weighting
 * @returns {number} - The interpolated G-weighting value
 */
function GWeighting(f) {
  // handle cases where f is outside the range
  if (f < gWeightingTable[0].f) {
    return gWeightingTable[0].gValue;
  } else if (f > gWeightingTable[gWeightingTable.length - 1].f) {
    return gWeightingTable[gWeightingTable.length - 1].gValue;
  }

  // interpolate between two nearest neigbours
  // linear search because of low number of elements in list
  for (let i = 0; i < gWeightingTable.length - 1; i++) {
    const f_low = gWeightingTable[i].f;
    const f_high = gWeightingTable[i + 1].f;

    if (f >= f_low && f <= f_high) {
      const g_low = gWeightingTable[i].gValue;
      const g_high = gWeightingTable[i + 1].gValue;
      return g_low + ((f - f_low) / (f_high - f_low)) * (g_high - g_low);
    }
  }
}

const gWeightingCache = {};

function computeDBGSpectrum(frequencies, spectrum) {
  const result_spectrum = computeSPLSpectrum(frequencies, spectrum);
  if (!gWeightingCache[frequencies]) {
    const gWeightings = new Array(frequencies.length);
    for (let i = 0; i < frequencies.length; i++) {
      gWeightings[i] = GWeighting(frequencies[i]);
    }
    gWeightingCache[frequencies] = gWeightings;
  }
  // retrieve precomputed aWeightings
  const gWeightings = gWeightingCache[frequencies];

  for (let i = 0; i < spectrum.length; i++) {
    result_spectrum[i] = result_spectrum[i] + gWeightings[i];
  }
  return result_spectrum;
}

function computeTotalDBG(frequencies, spectrum) {
  const dbg_spectrum = computeDBGSpectrum(frequencies, spectrum);
  const linear_spectrum = dbg_spectrum.map((value) => 10 ** (value / 10));
  const total_linear_perassure = linear_spectrum.reduce(
    (sum, value) => sum + value,
    0,
  );
  const result = 10 * Math.log10(total_linear_perassure);
  return result;
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

/**
 * Function to compute the mean of a Float32Array using Kahan summation for numerical stability
 * @param {Float32Array} data - The input array
 * @returns {number} - The mean of the elements in the array
 */
function stableMeanOfFloat32Array(data) {
  let sum = 0;
  let compensation = 0; // This is the compensation term for lost low-order bits
  const len = data.length;

  for (let i = 0; i < len; i++) {
    const y = data[i] - compensation; // Correct the next value
    const t = sum + y; // Accumulate the corrected value
    compensation = (t - sum) - y; // Compute the new compensation
    sum = t; // Update the sum with the corrected value
  }

  return sum / len;
}

function fourier_transform(buffer) {
  const mean = stableMeanOfFloat32Array(buffer);
  buffer = buffer.map((value) => value - mean);
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
    buffer.length,
  );
  fft_squared_magnitudes = fft_squared_magnitudes.slice(
    0,
    fft_squared_magnitudes.length / 2,
  );

  const scaled_magnitudes = fft_squared_magnitudes.map(
    (value) => (2 * value ** 0.5) / buffer.length,
  );

  return scaled_magnitudes;
}
