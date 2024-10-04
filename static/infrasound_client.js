let start_timestamp = 0;
const ms_between_measurements = 20;

let number_of_new_measurements = 0;

let computeTotalNoise = null;
let computeSpectrumFromSquaredMagnitudes = null;

pfft_module = null;
pffft().then(function (Module) {
  console.log("PFFFT Module initialized");
  pffft_module = Module;
});
let chartTimeSeriesDuration = 30 * 50;
const chartTimeSeries = new Highcharts.Chart({
  chart: {
    renderTo: "infrasound-time-serie",
    animation: false,
  },
  title: { text: "Zeit Series" },
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
    type: "datetime",
    dateTimeLabelFormats: { second: "%H:%M:%S" },
  },
  yAxis: {
    title: { text: "Relativer Luftdruck [Pa]" },
    gridLineWidth: 1,
  },
  credits: { enabled: false },
  time: {
    timezone: "Europe/Berlin",
  },
});

const chartSpectrum = new Highcharts.Chart({
  chart: {
    renderTo: "infrasound-spectrum",
    animation: false,
  },
  title: { text: "Spektrum" },
  series: [
    {
      showInLegend: false,
      data: [],
      type: "line",
    },
    {
      showInLegend: false,
      data: [],
      type: "column",
    },
  ],
  plotOptions: {
    line: {
      dataLabels: { enabled: false },
      lineWidth: 5,
    },
    column: {
      pointPadding: 0,
      borderWidth: 0,
      groupPadding: 0,
      shadow: false,
    },
    series: {
      color: "#059e8a",
    },
  },
  xAxis: {
    title: { text: "Frequenz [Hz]" },
    type: "linear",
  },
  yAxis: {
    title: { text: "Amplitude [Pa]" },
    gridLineWidth: 1,
  },
  credits: { enabled: false },
});

let measurement_buffer = [];
let times_buffer = [];
let index_buffer = [];
let pffft_runner = null;
let dataPtr = null;
let dataHeap = null;

function cleanup_pfft() {
  pffft_module._free(dataPtr);
  pffft_module._pffft_runner_destroy(pffft_runner);
  dataPtr = null;
  pffft_runner = null;
}

function initialize_pfft(fft_window) {
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

function fourier_transform(timeSequence) {
  const buffer = new Float32Array(timeSequence);

  // Copy data to Emscripten heap (directly accessed from Module.HEAPU8)
  dataHeap.set(new Uint8Array(buffer.buffer));

  // Call function and get result
  pffft_module._pffft_runner_transform_magnitudes(
    pffft_runner,
    dataHeap.byteOffset,
  );

  const fft_squared_magnitudes = new Float32Array(
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

function setTotalNoise(totalValue) {
  const totalValueElement = document.getElementById("totalValue");
  totalValueElement.innerText = totalValue.toFixed(4);
}

function updateCharts() {
  // We never show data older than 5 minutes = 15000 samples @ 50 Hz
  measurement_buffer = measurement_buffer.slice(-15000);
  times_buffer = times_buffer.slice(-15000);
  index_buffer = index_buffer.slice(-15000);

  // Update data in time chart
  chart_data_time = times_buffer.slice(-chartTimeSeriesDuration);
  chart_data_preassure = measurement_buffer.slice(-chartTimeSeriesDuration);
  chart_data = chart_data_time.map(function (val, idx) {
    return [val, chart_data_preassure[idx]];
  });
  chartTimeSeries.series[0].setData(chart_data, false, false, false);
  chartTimeSeries.update({}, true, false, false);

  const fft_window = 2 ** document.getElementById("spectrumRange").value;
  let fft_time_sequence = Array.from(measurement_buffer.slice(-fft_window));
  if (fft_time_sequence.length < fft_window) {
    // pad up with zeros
    const padding = Array(fft_window - fft_time_sequence.length).fill(0);
    fft_time_sequence = fft_time_sequence.concat(padding);
  }

  if (!pffft_module) {
    console.log("pffft module not yet loaded...");
    return;
  }
  if (!pffft_runner) {
    console.log("Initializing pffft");
    initialize_pfft(fft_window);
    console.log("pffft initialized");
  }

  const spectrum = fourier_transform(fft_time_sequence);
  const frequencies = Float32Array(spectrum.length);
  for (let i = 0; i < spectrum.length; i++) {
    const hz = (i * 50.0) / fft_time_sequence.length;
    frequencies[i] = hz;
  }
  scaled_spectrum = computeSpectrumFromSquaredMagnitudes(frequencies, spectrum);
  totalNoise = computeTotalNoise(frequencies, spectrum);
  setTotalNoise(totalNoise);

  updateSpectrogram(scaled_spectrum.slice(1));

  const spectrumChartData = [];
  for (let i = 0; i < scaled_spectrum.length; i++) {
    spectrumChartData[i] = [frequencies[i], scaled_spectrum[i]];
  }
  chartSpectrum.series[0].setData(spectrumChartData, false, false, false);
  chartSpectrum.series[1].setData(spectrumChartData, false, false, false);
  chartSpectrum.update({}, true, false, false);
}

// load the start-timestamp
const start_timestamp_request = new XMLHttpRequest();

start_timestamp_request.onreadystatechange = function () {
  if (
    start_timestamp_request.readyState == XMLHttpRequest.DONE &&
    start_timestamp_request.status == 200
  ) {
    const responseText = start_timestamp_request.responseText;
    console.log("Initial timestamp: ", responseText);
    start_timestamp = parseInt(responseText);
    console.log("Loading initial measurements...");
    setupEventListener();
  }
};
start_timestamp_request.open("GET", "/start_timestamp", true);
start_timestamp_request.send();

function setupEventListener() {
  // setup event listener for new measurements
  console.log("setupEventListener");
  if (!self.EventSource) {
    console.log("Creating EventSource");
    const source = new EventSource("/measurement_events");

    source.addEventListener(
      "open",
      function (e) {
        console.log("Events connected");
      },
      false,
    );

    source.addEventListener(
      "error",
      function (e) {
        if (e.target.readyState != EventSource.OPEN) {
          console.log("Events Disconnected");
        }
      },
      false,
    );

    source.addEventListener(
      "measurement",
      function (e) {
        // console.log("measurement event", e.data);
        message = e.data.split(";");
        if (message.length != 2) {
          console.log("ERROR: message length is expected to be 2, was:", message.length);
          return;
        }
        const index_string = message[0];
        const meassurement_string = message[1];

        const new_index = parseInt(index_string);
        const new_measurement = parseFloat(meassurement_string);
        measurement_buffer.push(new_measurement);
        let new_timestamp;
        if (times_buffer.length == 0) {
          new_timestamp = start_timestamp + new_index * ms_between_measurements;
        } else {
          const start_idx = index_buffer[index_buffer.length - 1];
          const time_since_start =
            (new_index - start_idx) * ms_between_measurements;
          new_timestamp =
            times_buffer[times_buffer.length - 1] + time_since_start;
        }
        times_buffer.push(new_timestamp);
        index_buffer.push(new_index);
        number_of_new_measurements += 1;
        if (number_of_new_measurements > 10) {
          updateCharts();
          number_of_new_measurements = 0;
        }
      },
      false,
    );
  }
}

// Time Series range
document.getElementById("timeSeriesRange").value = chartTimeSeriesDuration;
document.getElementById("timeSeriesRangeIndicator").innerHTML =
  "Dargestellte Zeitspanne: " + chartTimeSeriesDuration / 50 + " seconds";
// Slider for time duration
document.getElementById("timeSeriesRange").oninput = function () {
  const val = document.getElementById("timeSeriesRange").value;
  chartTimeSeriesDuration = val;
  document.getElementById("timeSeriesRangeIndicator").innerHTML =
    "Dargestellte Zeitspanne: " + val / 50 + " seconds";
};

// Spectrum range
document.getElementById("SpectrumRangeIndicator").innerHTML =
  "Spektrum Analyse Dauer: " +
  2 ** document.getElementById("spectrumRange").value / 50.0 +
  " seconds (" +
  2 ** document.getElementById("spectrumRange").value +
  " samples)";
document.getElementById("spectrumRange").oninput = function () {
  const val = 2 ** document.getElementById("spectrumRange").value;
  cleanup_pfft();
  initialize_pfft(val);
  document.getElementById("SpectrumRangeIndicator").innerHTML =
    "Spektrum Analyse Dauer: " + val / 50.0 + " seconds (" + val + " samples)";
  resizeCanvas();
};

// By default use linear spectrum and hide line chart
chartSpectrum.series[0].hide();

// Spectrum logarithmic
function handleSpectrumLogSwitch(checkbox) {
  if (checkbox.checked) {
    console.log("Spectrum Log enabled");
    chartSpectrum.xAxis[0].update({ type: "logarithmic" });
    chartSpectrum.series[0].show();
  } else {
    console.log("Spectrum Log disabled");
    chartSpectrum.xAxis[0].update({ type: "linear" });
    chartSpectrum.series[0].hide();
  }
}

function computeTotalRMS(frequencies, spectrum) {
  let rms = 0.0;
  rms = spectrum
    .slice(1)
    .map((value) => value ** 2)
    .reduce((sum, value) => sum + value, 0); // skip the first entry with static content
  rms = rms / (spectrum.length - 1);
  rms = Math.sqrt(rms);
  return rms;
}
computeTotalNoise = computeTotalRMS;

function computeTotalSPL(frequencies, spectrum) {
  const rms = computeTotalRMS(frequencies, spectrum);
  const p_ref = 20e-6; // Reference value for SPL 20 micro pascal
  const spl = 20 * Math.log10(rms / p_ref);
  return spl;
}

function computeRMSSpectrum(frequencies, spectrum) {
  return spectrum;
}
computeSpectrumFromSquaredMagnitudes = computeRMSSpectrum;

function computeSPLSpectrum(frequencies, spectrum) {
  const sqrt_2 = Math.sqrt(2);
  const p_ref = 20e-6; // Reference value for SPL 20 micro pascal
  return spectrum.map((value) => 20 * Math.log10(value / sqrt_2 / p_ref));
}
function AWeighting(f) {
  // Coefficients for A-weighting formula
  const c1 = 12200 ** 2;
  const c2 = 20.6 ** 2;
  const c3 = 107.7 ** 2;
  const c4 = 737.9 ** 2;

  const f2 = f ** 2;
  const f4 = f2 ** 2;

  // Calculate A-weighting in linear scale
  const numerator = c1 * f4;
  const denominator = (f2 + c2) * Math.sqrt((f2 + c3) * (f2 + c4)) * (f2 + c1);

  // A-weighting and convert to dB
  return 20 * Math.log10(numerator / denominator) + 2.0; // A-weighting has a +2.0 dB offset
}

const aWeightingCache = {};

function computeDBASpectrum(frequencies, spectrum) {
  const result_spectrum = computeSPLSpectrum(frequencies, spectrum);
  if (!aWeightingCache[frequencies]) {
    const aWeightings = new Array(frequencies.length);
    for (let i = 0; i < frequencies.length; i++) {
      aWeightings[i] = AWeighting(frequencies[i]);
    }
    aWeightingCache[frequencies] = aWeightings;
  }
  // retrieve precomputed aWeightings
  const aWeightings = aWeightingCache[frequencies];

  for (let i = 0; i < spectrum.length; i++) {
    result_spectrum[i] = result_spectrum[i] + aWeighting(i);
  }
  return result_spectrum;
}

function computeTotalDBA(frequencies, spectrum) {
  const dba_spectrum = computeDBASpectrum(frequencies, spectrum);
  const linear_spectrum = dba_spectrum.map((value) => 10 ** (value / 10));
  const total_linear_perassure = linear_spectrum.reduce(
    (sum, value) => sum + value,
    0,
  );
  const result = 10 * Math.log10(total_linear_perassure);
  return result;
  // return 10 * Math.log10(dba_spectrum.reduce((sum, value) => sum + 10 ** (value / 10), 0));
}

function handleAmplitudeUnitSwitch(radio) {
  const choice = radio.id;
  switch (choice) {
    case "usePa": {
      const totalTitleLabel = document.getElementById("totalTitleLabel");
      totalTitleLabel.innerText = "Effektivwert des Schalldrucks:";
      const totalUnit = document.getElementById("totalUnit");
      totalUnit.innerText = "Pascal (Pa)";
      const infoRMS = document.getElementById("infoRMS");
      const infoSPL = document.getElementById("infoSPL");
      const infoDBA = document.getElementById("infoDBA");
      infoRMS.style.display = "block";
      infoSPL.style.display = "none";
      infoDBA.style.display = "none";

      computeTotalNoise = computeTotalRMS;
      computeSpectrumFromSquaredMagnitudes = computeRMSSpectrum;

      chartSpectrum.yAxis[0].axisTitle.textStr = "Amplitude [Pa]";

      break;
    }
    case "useSPL": {
      const totalTitleLabel = document.getElementById("totalTitleLabel");
      totalTitleLabel.innerText = "Dauerschallpegel:";
      const totalUnit = document.getElementById("totalUnit");
      totalUnit.innerText = "db(SPL)";
      const infoRMS = document.getElementById("infoRMS");
      const infoSPL = document.getElementById("infoSPL");
      const infoDBA = document.getElementById("infoDBA");
      infoRMS.style.display = "none";
      infoSPL.style.display = "block";
      infoDBA.style.display = "none";

      computeTotalNoise = computeTotalSPL;
      computeSpectrumFromSquaredMagnitudes = computeSPLSpectrum;
      chartSpectrum.yAxis[0].axisTitle.textStr = "Schallpegel [dB(SPL)]";
      break;
    }
    case "useDbA": {
      const totalTitleLabel = document.getElementById("totalTitleLabel");
      totalTitleLabel.innerText = "A-bewerteter Schallpegel";
      const totalUnit = document.getElementById("totalUnit");
      totalUnit.innerText = "db(A)";
      const infoRMS = document.getElementById("infoRMS");
      const infoSPL = document.getElementById("infoSPL");
      const infoDBA = document.getElementById("infoDBA");
      infoRMS.style.display = "none";
      infoSPL.style.display = "none";
      infoDBA.style.display = "block";

      computeTotalNoise = computeTotalDBA;
      computeSpectrumFromSquaredMagnitudes = computeDBASpectrum;
      chartSpectrum.yAxis[0].axisTitle.textStr = "Schallpegel [dB(A)]";
      //todo
      break;
    }
    default: {
      console.log("WARNING: got unexpected choice for amplitude unit", choice);
    }
  }
  // Reset canvas
  resizeCanvas();
}

// Constants for spectrogram dimensions
let canvasWidth = 800; // Number of time steps (X-axis)
const fft_window = 2 ** document.getElementById("spectrumRange").value;
let canvasHeight = fft_window / 2 - 1; // Number of frequency bins (Y-axis) except for the constant (first) frequency

// Set up Pixi.js application
const app = new PIXI.Application({
  width: canvasWidth,
  height: canvasHeight,
  backgroundColor: 0x000000, // Black background
  resolution: self.devicePixelRatio || 1,
  autoDensity: true,
});

document.getElementById("SpectrumDiv").appendChild(app.view);

// Create a texture to display the spectrogram
let spectrogramTexture = PIXI.Texture.fromBuffer(
  new Uint8Array(canvasWidth * canvasHeight * 4),
  canvasWidth,
  canvasHeight,
);
const spectrogramSprite = new PIXI.Sprite(spectrogramTexture);
app.stage.addChild(spectrogramSprite);

// Spectrogram buffer (to keep track of data over time)
let spectrogramBufferRaw = new Float32Array(canvasWidth * canvasHeight);
let spectrogramBuffer = new Uint8Array(canvasWidth * canvasHeight * 4); // 4 for RGBA

// Precompute heatmap table for all intensity values (0–255)
const heatmapTable = new Uint8Array(256 * 3); // 3 values (r, g, b) per intensity

for (let i = 0; i < 256; i++) {
  let r = 0,
    g = 0,
    b = 0;

  if (i <= 127) {
    g = Math.floor((i / 127) * 255);
    b = 255;
  } else if (i <= 191) {
    g = 255;
    b = Math.floor(255 - ((i - 127) / 64) * 255);
  } else {
    r = Math.floor(((i - 191) / 64) * 255);
    g = Math.floor(255 - ((i - 191) / 64) * 255);
  }

  heatmapTable[i * 3] = r;
  heatmapTable[i * 3 + 1] = g;
  heatmapTable[i * 3 + 2] = b;
}

// Function to update the spectrogram with heatmap colors
function updateSpectrogram(newSpectrum) {
  // Shift the current spectrogram buffer to the left
  for (let y = 0; y < canvasHeight; y++) {
    const rowStartIdx = y * canvasWidth;
    // move row one column to the left and drop the first element
    spectrogramBufferRaw.set(
      spectrogramBufferRaw.subarray(rowStartIdx + 1, rowStartIdx + canvasWidth),
      rowStartIdx,
    );
  }

  // Insert the new spectrum into the rightmost column with heatmap colors
  for (let y = 0; y < newSpectrum.length; y++) {
    const index = y * canvasWidth + (canvasWidth - 1);
    spectrogramBufferRaw[index] = newSpectrum[y];
  }

  // get value range
  let maxVal = spectrogramBufferRaw.reduce(
    (max, value) => Math.max(max, value),
    Number.MIN_VALUE,
  );
  let minVal = spectrogramBufferRaw.reduce(
    (min, value) => Math.min(min, value),
    Number.MAX_VALUE,
  );

  console.log("DEBUG min:", minVal, "max:", maxVal);
  if (minVal === Number.NEGATIVE_INFINITY) {
    console.log(newSpectrum);
  }
  if (maxVal == minVal) {
    maxVal = 1;
    minVal = 0;
  }

  // normalizedSpectrum
  const normalizedSpectrum = spectrogramBufferRaw.map((value) =>
    Math.floor(((value - minVal) / (maxVal - minVal)) * 255),
  );
  // apply heatmap
  for (let i = 0; i < normalizedSpectrum.length; i++) {
    const intensity = normalizedSpectrum[i];
    const index = i * 4;
    spectrogramBuffer[index] = heatmapTable[intensity * 3];
    spectrogramBuffer[index + 1] = heatmapTable[intensity * 3 + 1];
    spectrogramBuffer[index + 2] = heatmapTable[intensity * 3 + 2];
    spectrogramBuffer[index + 3] = 255;
  }

  // Update the texture with the new buffer data
  spectrogramTexture.baseTexture.update(
    spectrogramBuffer,
    canvasWidth,
    canvasHeight,
  );
}

function resizeCanvas() {
  const newHeight = 2 ** document.getElementById("spectrumRange").value / 2 - 1;
  const newWidth = document.getElementById("SpectrumDiv").offsetWidth;
  // Update the application renderer dimensions (this will resize the canvas)
  app.renderer.resize(newWidth, newHeight);

  // Update global variables with the new dimensions
  canvasWidth = newWidth;
  canvasHeight = newHeight;

  // clean up old texture
  if (spectrogramTexture) {
    spectrogramTexture.destroy(true);
  }
  // Recreate the spectrogram buffer to match the new size
  spectrogramBufferRaw = new Float32Array(newWidth * newHeight);
  spectrogramBuffer = new Uint8Array(newWidth * newHeight * 4); // 4 for RGBA

  // Recreate the texture to match the new size
  spectrogramTexture = PIXI.Texture.fromBuffer(
    spectrogramBuffer,
    newWidth,
    newHeight,
  );

  // Update the spectrogram sprite with the new texture
  spectrogramSprite.texture = spectrogramTexture;
}

// Resize spectrogram when the browser is resized
document.addEventListener("DOMContentLoaded", function () {
  resizeCanvas();
});
addEventListener("resize", (event) => {
  resizeCanvas();
});
