let start_timestamp = 0;
const ms_between_measurements = 20;
const spectrumUpdateFrequency = 10;

let number_of_new_measurements = 0;

let computeTotalNoise = null;
let computeSpectrumFromSquaredMagnitudes = null;

pfft_module = null;
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

function updateCharts() {
  // We never show data older than 5 minutes = 15000 samples @ 50 Hz
  measurement_buffer = measurement_buffer.slice(-15000);
  times_buffer = times_buffer.slice(-15000);
  index_buffer = index_buffer.slice(-15000);

  // Update data in time chart
  chart_data_time = times_buffer.slice(-chartTimeSeriesDuration);
  chart_data_preassure = measurement_buffer.slice(-chartTimeSeriesDuration);
  chart_data = chart_data_time.map(function(val, idx) {
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
  const frequencies = new Float32Array(spectrum.length);
  for (let i = 0; i < spectrum.length; i++) {
    const hz = (i * 50.0) / fft_time_sequence.length;
    frequencies[i] = hz;
  }
  updateSpectrogram(
    spectrum,
    times_buffer[times_buffer.length - 1],
    spectrumUpdateFrequency * 50,
  );

  scaled_spectrum = computeSpectrumFromSquaredMagnitudes(frequencies, spectrum);
  totalNoise = computeTotalNoise(frequencies, spectrum);
  setTotalNoise(totalNoise);

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
    result_spectrum[i] = result_spectrum[i] + aWeightings[i];
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

let currentRenderMode = 0;
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
      const infoDBA = document.getElementById("infoDBA");
      infoRMS.style.display = "none";
      infoSPL.style.display = "block";
      infoDBA.style.display = "none";

      computeTotalNoise = computeTotalSPL;
      computeSpectrumFromSquaredMagnitudes = computeSPLSpectrum;
      chartSpectrum.yAxis[0].axisTitle.textStr = "Schallpegel [dB(SPL)]";
      currentRenderMode = 1;
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
      currentRenderMode = 2;
      break;
    }
    default: {
      console.log("WARNING: got unexpected choice for amplitude unit", choice);
    }
  }
}

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
uniform float uMinFrequency;
uniform float uMaxFrequency;
uniform int uRingbufferIndex;

// output color
out vec4 fragColor;

// function to compute SPL from input value
float computeSPL(float value) {
  const float sqrt_2 = 1.41421356237;
  const float p_ref = 20e-6; // Reference preassure for SPL
  return 20.0 * log(value / sqrt_2 / p_ref) / log(10.0);
}

// Function for A-weighting
float AWeighting(float f) {
  const float c1 = 12200.0 * 12200.0;
  const float c2 = 20.6 * 20.6;
  const float c3 = 107.7 * 107.7;
  const float c4 = 737.9 * 737.9;

  float f2 = f * f;
  float f4 = f2 * f2;

  // Calculate A-weighting in linear scale
  float numerator = c1 * f4;
  float denominator = (f2 + c2) * sqrt((f2 + c3) * (f2 + c4)) * (f2 + c1);
  
  // A-weighting and convert to dB
  return 20.0 * log(numerator / denominator) / log(10.0) + 2.0; // A-weighting has a +2.0 dB offset
}

// Function to compute dBA
float computeDBA(float value, float frequency) {
  float splValue = computeSPL(value);
  float aWeight = AWeighting(frequency);
  return splValue + aWeight;
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
    value = computeDBA(value, frequency);
    maxValue = computeDBA(maxValue, 24.9);
    minValue = computeDBA(minValue, 0.1);
    // Make sure min value is not -inf if an amplitude ever really gets 0
    save_min_value = max(minValue, -120.0);
    value = clamp(value, save_min_value, maxValue);
  }
  
  // Normalize spectrogram to [0, 1]
  value = (value - save_min_value) / (maxValue - save_min_value);
  // get color
  vec3 color = heatmapColor(value);
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
  const height = 2 ** document.getElementById("spectrumRange").value / 2 - 2;
  ringbuffer = new RingBufferTexture(gl, width, height);
}

initWebGLSpectrogramogram();

function updateSpectrogram(newSpectrum, currentTime, updateIntervals) {
  if (!gl) {
    console.warn("No webgl available - I won't update the spectrogram");
    return;
  }
  // The first entry in the new spectrum is just a constant (frequency = 0).
  // We remove it here and don't include it in the spectrogram
  newSpectrum = newSpectrum.slice(1);
  ringbuffer.addColumn(newSpectrum);
  renderSpectrogram();

  updateSpectrogramLabels(currentTime, updateIntervals);
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
    ctx.moveTo(0, y);
    ctx.lineTo(labelCanvas.width, y);
    ctx.strokeStyle = "rgba(211, 211, 211, 0.5)";
    ctx.lineWidth = 1;
    ctx.stroke();
  }
  for (let i = 0; i <= numXLabels; i++) {
    const x = labelCanvas.width * (1 - i / numXLabels);
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, labelCanvas.height);
    ctx.strokeStyle = "rgba(211, 211, 211, 0.5)";
    ctx.lineWidth = 1;
    ctx.stroke();
  }

  // draw y-axis labels (frequencies)
  const freqMin = 25 / labelCanvas.height;
  const freqMax = 25;
  for (let i = 0; i <= numYLabels - 1; i++) {
    const y = ((0.5 + i) / numYLabels) * labelCanvas.height;
    const freq = freqMin + ((i + 0.5) / numYLabels) * (freqMax - freqMin);
    ctx.fillStyle = "white";
    ctx.font = "14px Arial";
    ctx.fillText(`${freq.toFixed(1)} Hz`, 10, y);
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
    ctx.fillStyle = "white";
    ctx.font = "14px Arial";
    ctx.fillText(timeString, x, labelCanvas.height - 10);
  }
}

function renderSpectrogram() {
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
  const minFrequency = (1 + ringbuffer.minIdx) / (ringbuffer.height + 1);
  const maxFrequency = (1 + ringbuffer.maxIdx) / (ringbuffer.height + 1);
  gl.uniform1f(
    gl.getUniformLocation(shaderProgram, "uMinFrequency"),
    minFrequency,
  );
  gl.uniform1f(
    gl.getUniformLocation(shaderProgram, "uMaxFrequency"),
    maxFrequency,
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
  const newWidth = container.offsetWidth;

  // get canvases
  const webglCanvas = document.getElementById("webglCanvas");
  const labelCanvas = document.getElementById("labelCanvas");

  // set new canvas sizes
  webglCanvas.width = labelCanvas.width = newWidth;
  webglCanvas.height = labelCanvas.height = newHeight;

  ringbuffer.resize(newWidth, newHeight);
  renderSpectrogram();

  // set height of parent div
  container.style.height = `${newHeight}px`;

  gl.viewport(0, 0, webglCanvas.width, webglCanvas.height);
}

// Resize spectrogram when the browser is resized
document.addEventListener("DOMContentLoaded", function() {
  resizeCanvas();
});

addEventListener("resize", (event) => {
  resizeCanvas();
});

// DEBUG:
// setInterval(() => {
//   const newHeight = 2 ** document.getElementById("spectrumRange").value / 2 - 1;
//   //const newData = new Float32Array(newHeight).map(() => Math.random());
//   const newData = new Float32Array(newHeight).map((_, i) => 1+1e-3+Math.sin(5*2*Math.PI*i/newHeight) + Math.random()*0.2);
//
//   updateSpectrogram(newData, Date(), spectrumUpdateFrequency * 50);
// }, 50 * spectrumUpdateFrequency);
