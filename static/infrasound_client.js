let start_timestamp = 0;
const ms_between_measurements = 20;
const spectrumUpdateFrequency = 10;

let number_of_new_measurements = 0;

let last_index_in_spectrogram = -1;
let spectrogram_overlap_fraction = 0.9;

// min and max value in color range for db(G)
const minDbG = 0;
const maxDbG = 100;

let computeTotalNoise = null;
let computeSpectrumFromSquaredMagnitudes = null;

// options for showing or hiding elements
let isTimeSeriesShown = true;
let isSpectrumShown = false;
let isSpectrogramShown = false;
// Make usre the checkboxes reflect the initial state
document.getElementById("isTimeSeriesShown").checked = isTimeSeriesShown;
document.getElementById("isSpectrumShown").checked = isSpectrumShown;
document.getElementById("isSpectrogramShown").checked = isSpectrogramShown;

pffft_module = null;
pffft().then(function(Module) {
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
  mean = stableMeanOfFloat32Array(timeSequence);
  timeSequence = timeSequence.map((value) => value - mean);
  const buffer = new Float32Array(timeSequence);

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

function setTotalNoise(totalValue) {
  const totalValueElement = document.getElementById("totalValue");
  totalValueElement.innerText = totalValue.toFixed(4);
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

function updateCharts() {

  // We never show data older than 5 minutes = 15000 samples @ 50 Hz
  measurement_buffer = measurement_buffer.slice(-15000);
  times_buffer = times_buffer.slice(-15000);
  index_buffer = index_buffer.slice(-15000);

  // Update data in time chart
  if (isTimeSeriesShown) {
    chart_data_time = times_buffer.slice(-chartTimeSeriesDuration);
    chart_data_preassure = measurement_buffer.slice(-chartTimeSeriesDuration);
    chart_data = chart_data_time.map(function(val, idx) {
      return [val, chart_data_preassure[idx]];
    });
    chartTimeSeries.series[0].setData(chart_data, false, false, false);
    chartTimeSeries.update({}, true, false, false);
  }

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
  const frequencies = new Float32Array(spectrum.length);
  for (let i = 0; i < spectrum.length; i++) {
    const hz = (i * 50.0) / fft_time_sequence.length;
    frequencies[i] = hz;
  }
  // Spectrogram should always be updated even when it is hidden, otherwise there will be disconituities whenever it is hidden
  last_index = index_buffer[index_buffer.length - 1];
  if (last_index - last_index_in_spectrogram > fft_window * (1 - spectrogram_overlap_fraction)) {
    console.log("Updating spectrogram");
    last_index_in_spectrogram = last_index;
    var update_frequency = fft_window * (1 - spectrogram_overlap_fraction) * 50;
    if (spectrogram_overlap_fraction >= 1) {
      update_frequency = 50 * spectrumUpdateFrequency;
    }
    updateSpectrogram(
      spectrum,
      times_buffer[times_buffer.length - 1],
      update_frequency,
    );
  }


  scaled_spectrum = computeSpectrumFromSquaredMagnitudes(frequencies, spectrum);
  totalNoise = computeTotalNoise(frequencies, spectrum);
  setTotalNoise(totalNoise);

  if (isSpectrumShown) {
    const spectrumChartData = [];
    for (let i = 0; i < scaled_spectrum.length; i++) {
      spectrumChartData[i] = [frequencies[i], scaled_spectrum[i]];
    }
    chartSpectrum.series[0].setData(spectrumChartData, false, false, false);
    chartSpectrum.series[1].setData(spectrumChartData, false, false, false);
    chartSpectrum.update({}, true, false, false);
  }
}

// load the start-timestamp
const start_timestamp_request = new XMLHttpRequest();

start_timestamp_request.onreadystatechange = function() {
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
  console.log("Creating EventSource");
  const source = new EventSource("/measurement_events");
  // Tell the esp to start taking measurements

  source.addEventListener(
    "open",
    function(e) {
      console.log("Events connected");
    },
    false,
  );

  source.addEventListener(
    "error",
    function(e) {
      if (e.target.readyState != EventSource.OPEN) {
        console.log("Events Disconnected");
      }
    },
    false,
  );

  console.log("Adding the event listener for measurement message");
  console.log(source);
  source.addEventListener(
    "measurement",
    function(e) {
      // console.log("got measurement event", e.data);
      message = e.data.split(";");
      if (message.length != 2) {
        console.log(
          "ERROR: message length is expected to be 2, was:",
          message.length,
        );
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

      if (number_of_new_measurements > spectrumUpdateFrequency) {
        updateCharts();
        number_of_new_measurements = 0;
      }
    },
    false,
  );
  startMeaurements();
}

function startMeaurements() {
  const start_measurement_request = new XMLHttpRequest();

  start_timestamp_request.onreadystatechange = function() {
    if (
      start_timestamp_request.readyState == XMLHttpRequest.DONE &&
      start_timestamp_request.status == 200
    ) {
      console.log("Started Measurements...");
    }
  };
  start_timestamp_request.open("GET", "/start_measurements", true);
  start_timestamp_request.send();
}

// Time Series range
document.getElementById("timeSeriesRange").value = chartTimeSeriesDuration;
document.getElementById("timeSeriesRangeIndicator").innerHTML =
  "Dargestellte Zeitspanne: " + chartTimeSeriesDuration / 50 + " seconds";
// Slider for time duration
document.getElementById("timeSeriesRange").oninput = function() {
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
document.getElementById("spectrumRange").oninput = function() {
  const val = 2 ** document.getElementById("spectrumRange").value;
  document.getElementById("SpectrumRangeIndicator").innerHTML =
    "Spektrum Analyse Dauer: " + val / 50.0 + " seconds (" + val + " samples)";
  resizeCanvas();

  cleanup_pfft();
  initialize_pfft(val);
};

// By default use linear spectrum and hide line chart
chartSpectrum.series[0].hide();

function computeTotalRMS(frequencies, spectrum) {
  let rms = 0.0;
  rms = spectrum
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

let currentRenderMode = 0;
function handleAmplitudeUnitSwitch(choice) {
  switch (choice) {
    case "usePa": {
      const totalTitleLabel = document.getElementById("totalTitleLabel");
      totalTitleLabel.innerText = "Effektivwert des Schalldrucks (RMS):";
      const totalUnit = document.getElementById("totalUnit");
      totalUnit.innerText = "(Pa)";
      const infoRMS = document.getElementById("infoRMS");
      const infoSPL = document.getElementById("infoSPL");
      const infoDBG = document.getElementById("infoDBG");
      infoRMS.style.display = "block";
      infoSPL.style.display = "none";
      infoDBG.style.display = "none";

      computeTotalNoise = computeTotalRMS;
      computeSpectrumFromSquaredMagnitudes = computeRMSSpectrum;

      chartSpectrum.yAxis[0].axisTitle.textStr = "Amplitude [Pa]";
      currentRenderMode = 0;

      break;
    }
    case "useSPL": {
      const totalTitleLabel = document.getElementById("totalTitleLabel");
      totalTitleLabel.innerText = "Dauerschallpegel:";
      const totalUnit = document.getElementById("totalUnit");
      totalUnit.innerText = "db(SPL)";
      const infoRMS = document.getElementById("infoRMS");
      const infoSPL = document.getElementById("infoSPL");
      const infoDBG = document.getElementById("infoDBG");
      infoRMS.style.display = "none";
      infoSPL.style.display = "block";
      infoDBG.style.display = "none";

      computeTotalNoise = computeTotalSPL;
      computeSpectrumFromSquaredMagnitudes = computeSPLSpectrum;
      chartSpectrum.yAxis[0].axisTitle.textStr = "Schallpegel [dB(SPL)]";
      currentRenderMode = 1;
      break;
    }
    case "useDbG": {
      const totalTitleLabel = document.getElementById("totalTitleLabel");
      totalTitleLabel.innerText = "G-bewerteter Schallpegel";
      const totalUnit = document.getElementById("totalUnit");
      totalUnit.innerText = "db(G)";
      const infoRMS = document.getElementById("infoRMS");
      const infoSPL = document.getElementById("infoSPL");
      const infoDBG = document.getElementById("infoDBG");
      infoRMS.style.display = "none";
      infoSPL.style.display = "none";
      infoDBG.style.display = "block";

      computeTotalNoise = computeTotalDBG;
      computeSpectrumFromSquaredMagnitudes = computeDBGSpectrum;
      chartSpectrum.yAxis[0].axisTitle.textStr = "Schallpegel [dB(G)]";
      currentRenderMode = 2;
      break;
    }
    default: {
      console.log("WARNING: got unexpected choice for amplitude unit", choice);
    }
  }

  renderSpectrogram(minDbG, maxDbG);

  updateColormap(ringbuffer.min, ringbuffer.max, minDbG, maxDbG);
}

function handleIsTimeSeriesShownSwitch(checkbox) {
  isTimeSeriesShown = checkbox.checked;
  console.log("Visibility of time series:", isTimeSeriesShown);
  const timeSeriesContainer = document.getElementById("TimeSeriesContainer");
  if (isTimeSeriesShown) {
    timeSeriesContainer.style.removeProperty("display");
  } else {
    timeSeriesContainer.style.display = "none";
  }
}

function handleIsSpectrumShownSwitch(checkbox) {
  isSpectrumShown = checkbox.checked;
  console.log("Visibility of Spectrum:", isSpectrumShown);
  const spectrumContainer = document.getElementById("SpectrumContainer");
  if (isSpectrumShown) {
    spectrumContainer.style.removeProperty("display");
  } else {
    spectrumContainer.style.display = "none";
  }
}

function handleIsSpectrogramShownSwitch(checkbox) {
  isSpectrogramShown = checkbox.checked;
  console.log("Visibility of Spectrogram:", isSpectrogramShown);
  const spectrogramContainer = document.getElementById("SpectrogramContainer");
  if (isSpectrogramShown) {
    spectrogramContainer.style.removeProperty("display");
  } else {
    spectrogramContainer.style.display = "none";
  }
}

//////  SPECTROGRAM CODE

class RingBufferTexture {
  constructor(glctx, width, height) {
    this.gl = glctx;
    this.width = width;
    this.height = height;
    this.texture = this.gl.createTexture();
    this.gl.bindTexture(this.gl.TEXTURE_2D, this.texture);

    // Set texture parameters to avoid using mipmaps
    this.gl.texParameteri(
      this.gl.TEXTURE_2D,
      this.gl.TEXTURE_MIN_FILTER,
      this.gl.NEAREST,
    );
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
    const data = new Float32Array(width * height);
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
    this.columnMax = new Float32Array(width).fill(Number.NEGATIVE_INFINITY);
    this.columnMin = new Float32Array(width).fill(Number.POSITIVE_INFINITY);
    this.columnMaxIdx = new Int32Array(width);
    this.columnMinIdx = new Int32Array(width);
    this.max = Number.MIN_VALUE;
    this.min = Number.MAX_VALUE;
    this.maxIdx = -1;
    this.minIdx = -1;
    this.index = 0;
  }

  _positiveMod(a, b) {
    b = Math.abs(b);
    let remainder = a % b;
    if (remainder < 0) {
      remainder += b;
    }
    return remainder;
  }

  addColumn(newColumn) {
    this.index = this._positiveMod(this.index - 1, this.width);
    // Update the texture with the new column
    this.gl.bindTexture(this.gl.TEXTURE_2D, this.texture);
    this.gl.texSubImage2D(
      this.gl.TEXTURE_2D,
      0, // level
      this.index, // x-offset
      0, // y-offset
      1, // width of uploaded column
      this.height, // height of column
      this.gl.RED, // format
      this.gl.FLOAT, // type
      newColumn, // data
    );
    // update the minimum and maximum for the new column
    for (let i = 0; i < newColumn.length; i++) {
      if (newColumn[i] > this.columnMax[this.index]) {
        this.columnMax[this.index] = newColumn[i];
        this.columnMaxIdx[this.index] = i;
      } else if (newColumn[i] < this.columnMin[this.index]) {
        this.columnMin[this.index] = newColumn[i];
        this.columnMinIdx[this.index] = i;
      }
    }
    // Make sure that the max or min value are invalidated if the ringbuffer is overwritten
    // at the place of the old max or min value.
    const computeGlobalMinMax =
      this.maxIdx == this.index || this.minIdx == this.index || this.minIdx == -1 || this.maxIdx == -1;
    if (this.maxIdx == this.index) {
      this.max = Number.MIN_VALUE;
    }
    if (this.minIdx == this.index) {
      this.min = Number.MAX_VALUE;
    }
    // update the total minimum and maximum
    if (computeGlobalMinMax) {
      for (let i = 0; i < this.columnMax.length; i++) {
        if (this.columnMax[i] > this.max) {
          this.max = this.columnMax[i];
          this.maxIdx = i;
        } else if (this.columnMin[i] < this.min) {
          this.min = this.columnMin[i];
          this.minIdx = i;
        }
      }
    } else {
      if (this.columnMax[this.index] > this.max) {
        this.max = this.columnMax[this.index];
        this.maxIdx = this.index;
      } else if (this.columnMin[this.index] < this.min) {
        this.min = this.columnMin[this.index];
        this.minIdx = this.index;
      }
    }
  }

  resize(newWidth, newHeight) {
    this.width = newWidth;
    this.height = newHeight;
    // clear texture and allocate new texture memory if necessary
    this.gl.bindTexture(this.gl.TEXTURE_2D, this.texture);
    const data = new Float32Array(this.width * this.height);
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
    // Reset the min and max values and indices
    this.columnMax = new Float32Array(this.width).fill(Number.NEGATIVE_INFINITY);
    this.columnMin = new Float32Array(this.width).fill(Number.POSITIVE_INFINITY);
    this.columnMaxIdx = new Int32Array(this.width);
    this.columnMinIdx = new Int32Array(this.width);
    this.max = Number.MIN_VALUE;
    this.min = Number.MAX_VALUE;
    this.maxIdx = -1;
    this.minIdx = -1;
    // reset ringbuffer position
    this.index = 0;
    console.log("Resized ringbuffer to", this.width, this.height);
  }
}

function compileShader(gl, source, type) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);

  if (gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    return shader;
  } else {
    console.error("Error compiling shader:", gl.getShaderInfoLog(shader));
    gl.deleteShader(shader);
    return null;
  }
}

// Constants for spectrogram dimensions
let canvasWidth = 800; // Number of time steps (X-axis)
const fft_window = 2 ** document.getElementById("spectrumRange").value;
let canvasHeight = fft_window / 2 - 1; // Number of frequency bins (Y-axis) except for the constant (first) frequency

// create webgl canvas
const canvas = document.getElementById("webglCanvas");
// get webgl context
const gl = canvas.getContext("webgl2");

let ringbuffer = null;
let shaderProgram = null;

function initWebGLSpectrogramogram() {
  if (!gl) {
    console.warn("WebGL is not supported - drawing spectrogram not possible.");
    return;
  }
  // Create shader program
  const vertexShaderSource = `#version 300 es
precision highp float;
in vec2 aPosition;

void main() {
  gl_Position = vec4(aPosition, 0.0, 1.0);
}
`;
  const fragmentShaderSource = `#version 300 es
precision highp float;

// Ringbuffer data
uniform sampler2D uTexture;
// Render mode for the spectrogram
uniform int uRenderMode;
// minimum and maximum value for normalization
uniform float uMinValue;
uniform float uMaxValue;
uniform float uMinDbG;
uniform float uMaxDbG;
uniform int uRingbufferIndex;

// output color
out vec4 fragColor;

// function to compute SPL from input value
float computeSPL(float value) {
  const float sqrt_2 = 1.41421356237;
  const float p_ref = 20e-6; // Reference preassure for SPL
  return 20.0 * log(value / sqrt_2 / p_ref) / log(10.0);
}

// Function for G-Weighting
float GWeighting(float f) {
  const float[21] frequencies = float[21](
     0.25, 0.315,
     0.4, 0.5,
     0.63, 0.8,
     1.0, 1.25,
     1.6, 2.0,
     2.5, 3.15,
     4.0, 5.0,
     6.3, 8.0,
     10.0, 12.5,
     16.0, 20.0, 25.0 );
  const float[21] gValues = float[21](
    -88.0,
    -80.0, -72.1,
    -64.3, -56.6,
    -49.5, -43.0,
    -37.5, -32.6,
    -28.3, -24.1,
    -20.0, -16.0,
    -12.0, -8.0,
    -4.0, 0.0,
    4.0, 7.7,
    9.0, 3.7
 );

  if (f <= frequencies[0]) {
    return gValues[0];
  } else if (f >= frequencies[20]) {
    return gValues[20];
  }

  for (int i = 0; i < 20; ++i) {
    if (f >= frequencies[i] && f <= frequencies[i + 1]) {
        // Calculate the interpolation factor
        float factor = (f - frequencies[i]) / (frequencies[i + 1] - frequencies[i]);
        return mix(gValues[i], gValues[i + 1], factor);
    }
  }
}

// Function to compute dBG
float computeDBG(float value, float frequency) {
  float splValue = computeSPL(value);
  float gWeight = GWeighting(frequency);
  return splValue + gWeight;
}

// Simple color map from white-blue-green-yellow-red-black
vec3 rainbowColor(float value) {
    // Clamp value to the range [0, 1]
    value = clamp(value, 0.0, 1.0);

    const float scalars[6] = float[6](0.0, 0.2, 0.4, 0.6, 0.8, 1.0);

    const vec3 colors[6] = vec3[6](vec3(255,255,255),   // Scalar 0
                                   vec3(0, 112, 255),   // Scalar 0.2
                                   vec3(0, 255, 3),     // Scalar 0.4
                                   vec3(255, 255, 4),   // Scalar 0.6
                                   vec3(255, 2, 1),     // Scalar 0.8
                                   vec3(0, 0, 0)       // Scalar 1
                                  );

    // Interpolate between the colors
    vec3 color = colors[0]; // Default to the first color

    for (int i = 0; i < 5; ++i) {
        if (value >= scalars[i] && value <= scalars[i + 1]) {
            // Calculate the interpolation factor
            float factor = (value - scalars[i]) / (scalars[i + 1] - scalars[i]);
            color = mix(colors[i], colors[i + 1], factor);
            break;
        }
    }

    // Normalize the color to the [0, 1] range by dividing by 255.0
    return color / 255.0;
}

// Function to map a normalized value using a heatmap (inferno)
// For reference colors see: https://www.kennethmoreland.com/color-advice/
vec3 heatmapColor(float value) {
    // Clamp value to the range [0, 1]
    value = clamp(value, 0.0, 1.0);

    // Define the scalar values and corresponding colors from your table
    const float scalars[8] = float[8](0.0, 0.142857142857143, 0.285714285714286, 0.428571428571429, 
                                      0.571428571428571, 0.714285714285714, 0.857142857142857, 1.0);

    const vec3 colors[8] = vec3[8](vec3(0, 0, 4),       // Scalar 0
                                   vec3(40, 11, 84),    // Scalar 0.142857142857143
                                   vec3(101, 21, 110),  // Scalar 0.285714285714286
                                   vec3(159, 42, 99),   // Scalar 0.428571428571429
                                   vec3(212, 72, 66),   // Scalar 0.571428571428571
                                   vec3(245, 125, 21),  // Scalar 0.714285714285714
                                   vec3(250, 193, 39),  // Scalar 0.857142857142857
                                   vec3(252, 255, 164)  // Scalar 1
                                  );

    // Interpolate between the colors
    vec3 color = colors[0]; // Default to the first color

    for (int i = 0; i < 7; ++i) {
        if (value >= scalars[i] && value <= scalars[i + 1]) {
            // Calculate the interpolation factor
            float factor = (value - scalars[i]) / (scalars[i + 1] - scalars[i]);
            color = mix(colors[i], colors[i + 1], factor);
            break;
        }
    }

    // Normalize the color to the [0, 1] range by dividing by 255.0
    return color / 255.0;
}

void main() {
  // Calculate index in the ringbuffer based on fragment position
  vec2 shape = vec2(textureSize(uTexture, 0).xy);
  vec2 fragCoord = gl_FragCoord.xy;
  
  fragCoord.y = shape.y - fragCoord.y;
  fragCoord.x = (shape.x - fragCoord.x + float(uRingbufferIndex));
  vec2 texCoord = fragCoord / shape;
  texCoord.x = texCoord.x - floor(texCoord.x);
  float frequency_steps = 25.0 / (shape.y + 1.0);
  float frequency = frequency_steps * (1.0 + fragCoord.y);
  // float frequency = texCoord.y * 25.0;  // Assumes frequencies between 0 and 25 hz in the spectrogram
  float maxValue = uMaxValue;
  float minValue = uMinValue;

  // load texel coordinate for the current position
  float value = texture(uTexture, texCoord).r;
  
  float save_min_value = minValue;
  // Apply render mode transform
  if (uRenderMode == 1) {
    value = computeSPL(value);
    maxValue = computeSPL(maxValue);
    minValue = computeSPL(minValue);
    // Make sure min value is not -inf if an amplitude ever really gets 0
    save_min_value = max(minValue, -120.0);
    value = clamp(value, save_min_value, maxValue);
  } else if (uRenderMode == 2) {
    value = computeDBG(value, frequency);
    minValue = uMinDbG;
    maxValue = uMaxDbG;
    save_min_value = minValue;
    // Make sure min value is not -inf if an amplitude ever really gets 0
    value = clamp(value, save_min_value, maxValue);
  }
  
  // Normalize spectrogram to [0, 1]
  value = (value - save_min_value) / (maxValue - save_min_value);
  // get color
  vec3 color = rainbowColor(value);
  // vec3 color = heatmapColor(value);
  fragColor = vec4(color, 1.0);
}
`;
  const vertexShader = compileShader(gl, vertexShaderSource, gl.VERTEX_SHADER);
  const fragmentShader = compileShader(
    gl,
    fragmentShaderSource,
    gl.FRAGMENT_SHADER,
  );
  shaderProgram = gl.createProgram();
  gl.attachShader(shaderProgram, vertexShader);
  gl.attachShader(shaderProgram, fragmentShader);
  gl.linkProgram(shaderProgram);

  //check if linking program was successful
  if (!gl.getProgramParameter(shaderProgram, gl.LINK_STATUS)) {
    console.error(
      "Error linking program:",
      gl.getProgramInfoLog(shaderProgram),
    );
  }
  // create a rectangle
  const positionBuffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
  const positions = new Float32Array([
    -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1,
  ]);
  gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);

  // Set up attribute for vertex positions
  const positionLocation = gl.getAttribLocation(shaderProgram, "aPosition");
  gl.enableVertexAttribArray(positionLocation);
  gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);

  // create ringbuffer texture
  const width = document.getElementById("SpectrogramDiv").offsetWidth;
  const height = 2 ** document.getElementById("spectrumRange").value / 2 - 1;
  ringbuffer = new RingBufferTexture(gl, width, height);
}

initWebGLSpectrogramogram();

function updateSpectrogram(newSpectrum, currentTime, updateIntervals) {
  if (!gl) {
    console.warn("No webgl available - I won't update the spectrogram");
    return;
  }
  ringbuffer.addColumn(newSpectrum);

  renderSpectrogram(minDbG, maxDbG);

  updateSpectrogramLabels(currentTime, updateIntervals);
  updateColormap(ringbuffer.min, ringbuffer.max, minDbG, maxDbG);
}
function computeColorFromValue(value) {
  const scalars = [0.0, 0.2, 0.4, 0.6, 0.8, 1.0];

  const colors = [
    { "red": 255, "green": 255, "blue": 255 },
    { "red": 0, "green": 112, "blue": 255 },
    { "red": 0, "green": 255, "blue": 3 },
    { "red": 255, "green": 255, "blue": 4 },
    { "red": 255, "green": 2, "blue": 1 },
    { "red": 0, "green": 0, "blue": 0 },
  ];

  // Interpolate between the colors
  let color = colors[0]; // Default to the first color

  for (let i = 0; i < 5; i++) {
    if (value >= scalars[i] && value <= scalars[i + 1]) {
      // Calculate the interpolation factor
      const factor = (value - scalars[i]) / (scalars[i + 1] - scalars[i]);
      color.red = (1 - factor) * colors[i].red + factor * colors[i + 1].red;
      color.green = (1 - factor) * colors[i].green + factor * colors[i + 1].green;
      color.blue = (1 - factor) * colors[i].blue + factor * colors[i + 1].blue;
      return color;
    }
  }
  return color;
}

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

function updateColormap(minPa, maxPa, minDbG, maxDbG) {
  const colormapCanvas = document.getElementById("colormap");
  const ctx = colormapCanvas.getContext("2d");
  ctx.clearRect(0, 0, colormapCanvas.width, colormapCanvas.height);

  const padding = 50;
  // draw color (todo can be optimized by putting createion of imageData into resize())
  const imageData = ctx.createImageData(colormapCanvas.width, colormapCanvas.height);
  for (let x = 0; x < colormapCanvas.width - padding * 2; x++) {
    const color = computeColorFromValue(x / (colormapCanvas.width - padding * 2 - 1));
    for (let y = 0; y < 10; y++) {
      const index = 4 * ((y * colormapCanvas.width) + x + padding);
      imageData.data[index + 0] = color.red;
      imageData.data[index + 1] = color.green;
      imageData.data[index + 2] = color.blue;
      imageData.data[index + 3] = 255;
    }
  }
  ctx.putImageData(imageData, 0, 0);

  // Draw grid lines
  const numXLabels = Math.round((colormapCanvas.width - padding * 2) / 200);
  const start_x = padding;
  const stop_x = colormapCanvas.width - padding;
  const xs = linspace(start_x, stop_x, numXLabels)
  for (const x of xs) {
    ctx.beginPath();
    ctx.moveTo(x, 10);
    ctx.lineTo(x, 15);
    ctx.strokeStyle = "rgba(0, 0, 0, 0.5)";
    ctx.lineWidth = 1;
    ctx.stroke();
  }

  // // Draw labels
  spectrum = new Float32Array([minPa, maxPa]);
  frequencies = new Float32Array([0, 25]); // is ignored
  min_max = computeSpectrumFromSquaredMagnitudes(frequencies, spectrum);
  unit = "pa";
  if (currentRenderMode == 1) {
    unit = "db(SPL)";
  } else if (currentRenderMode == 2) {
    unit = "db(G)";
    // Fixed value range for db(G)
    min_max[0] = minDbG;
    min_max[1] = maxDbG;
  }

  for (const [i, x] of xs.entries()) {
    const weight = i / (numXLabels - 1);
    const value = (1 - weight) * min_max[0] + weight * min_max[1];
    const label = `${value.toExponential(1)} ${unit}`;
    const labelWidth = ctx.measureText(label).width;
    ctx.fillStyle = "black";
    ctx.font = "14px Arial";
    ctx.fillText(label, x - labelWidth / 2, colormapCanvas.height - 10);
  }
}


function updateSpectrogramLabels(currentTime, updateIntervals) {
  const labelCanvas = document.getElementById("labelCanvas");
  const ctx = labelCanvas.getContext("2d");
  ctx.clearRect(0, 0, labelCanvas.width, labelCanvas.height);

  // draw grid lines
  const numYLabels = Math.floor(labelCanvas.height / 50);
  const numXLabels = labelCanvas.width / 100;
  for (let i = 0; i <= numYLabels - 1; i++) {
    const y = ((0.5 + i) / numYLabels) * labelCanvas.height;
    ctx.beginPath();
    ctx.moveTo(80, y);
    ctx.lineTo(labelCanvas.width, y);
    ctx.strokeStyle = "rgba(0, 0, 0, 0.5)";
    ctx.lineWidth = 1;
    ctx.stroke();
  }
  for (let i = 0; i <= numXLabels; i++) {
    const x = labelCanvas.width * (1 - i / numXLabels);
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, labelCanvas.height);
    ctx.strokeStyle = "rgba(0, 0, 0, 0.5)";
    ctx.lineWidth = 1;
    ctx.stroke();
  }

  // draw y-axis labels (frequencies)
  const freqMin = 25 / labelCanvas.height;
  const freqMax = 25;
  for (let i = 0; i <= numYLabels - 1; i++) {
    const y = ((0.5 + i) / numYLabels) * labelCanvas.height;
    const freq = freqMin + ((i + 0.5) / numYLabels) * (freqMax - freqMin);
    const hz_label = `${freq.toFixed(1)} Hz`;
    const hz_label_height = 14;
    ctx.fillStyle = "black";
    ctx.font = "14px Arial";
    ctx.fillText(hz_label, 10, y + hz_label_height / 2);
  }

  // draw x-axis labels (time)
  const startTime = new Date(currentTime);
  const endTime = new Date(
    startTime.getTime() - updateIntervals * labelCanvas.width,
  );
  for (let i = 0; i <= numXLabels; i++) {
    const x = labelCanvas.width * (1 - i / numXLabels);
    const time = new Date(
      startTime.getTime() -
      (i * (startTime.getTime() - endTime.getTime())) / numXLabels,
    );
    const timeString = time.toLocaleTimeString("de-DE");
    const timeStringWidth = ctx.measureText(timeString).width;
    ctx.fillStyle = "black";
    ctx.font = "14px Arial";
    ctx.fillText(timeString, x - timeStringWidth, labelCanvas.height - 10);
  }
}

function renderSpectrogram(minDbG, maxDbG) {
  if (!gl) {
    console.warn("Can not render Spectrum - webgl not supported");
    return;
  }
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.useProgram(shaderProgram);

  // set uniforms
  gl.uniform1i(
    gl.getUniformLocation(shaderProgram, "uRenderMode"),
    currentRenderMode,
  );
  gl.uniform1f(
    gl.getUniformLocation(shaderProgram, "uMinValue"),
    ringbuffer.min,
  );
  gl.uniform1f(
    gl.getUniformLocation(shaderProgram, "uMaxValue"),
    ringbuffer.max,
  );
  gl.uniform1f(
    gl.getUniformLocation(shaderProgram, "uMinDbG"),
    minDbG,
  );
  gl.uniform1f(
    gl.getUniformLocation(shaderProgram, "uMaxDbG"),
    maxDbG,
  );
  gl.uniform1i(
    gl.getUniformLocation(shaderProgram, "uRingbufferIndex"),
    ringbuffer.index,
  );
  // bind texture
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, ringbuffer.texture);
  gl.drawArrays(gl.TRIANGLES, 0, 6); // Replace with your draw call
}

function resizeCanvas() {
  if (!gl) {
    console.warn("webgl not available");
    return;
  }
  const newHeight = 2 ** document.getElementById("spectrumRange").value / 2 - 2;
  const container = document.getElementById("SpectrogramDiv");
  const optionsContainer = document.getElementById("visibilityOptionsContainer");
  const newWidth = optionsContainer.offsetWidth;

  // get canvases
  const webglCanvas = document.getElementById("webglCanvas");
  const labelCanvas = document.getElementById("labelCanvas");
  const colormapCanvas = document.getElementById("colormap");

  // set new canvas sizes
  if (webglCanvas.width == newWidth && webglCanvas.height == newHeight) {
    // No change ... skip resize
    return;
  }
  webglCanvas.width = labelCanvas.width = newWidth;
  webglCanvas.height = labelCanvas.height = newHeight;
  colormapCanvas.width = newWidth;
  colormapCanvas.height = 50;

  ringbuffer.resize(newWidth, newHeight);

  // set height of parent div
  container.style.height = `${newHeight}px`;

  gl.viewport(0, 0, webglCanvas.width, webglCanvas.height);

  renderSpectrogram(minDbG, maxDbG);

  updateColormap(ringbuffer.min, ringbuffer.max, minDbG, maxDbG);
}

// Resize spectrogram when the browser is resized
document.addEventListener("DOMContentLoaded", function() {
  resizeCanvas();
});

addEventListener("resize", (event) => {
  resizeCanvas();
});

