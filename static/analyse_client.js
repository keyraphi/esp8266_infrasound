var pffft_module = null;
var measurementAnalyser = null;

pffft().then(function(Module) {
  console.log("PFFFT Module initialized");
  pffft_module = Module;
});

const fromSlider = document.querySelector('#fromSlider');
const toSlider = document.querySelector('#toSlider');
const fromInput = document.querySelector('#fromInput');
const toInput = document.querySelector('#toInput');
const analyseRangeButton = document.querySelector("#analyseRangeButton");

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
    const analyseButton = document.createElement("button");
    const fileName = downloadOptions["files"][i];
    labelDiv.textContent = fileName;
    downloadButton.classList.add("btn");
    analyseButton.classList.add("btn");
    downloadButton.classList.add("btn-secondary");
    analyseButton.classList.add("btn-primary");
    buttonGroupDiv.classList.add("btn-group");
    buttonGroupDiv.setAttribute("role", "group");
    buttonGroupDiv.setAttribute("aria-label", "Button Group");
    buttonGroupDiv.appendChild(downloadButton);
    buttonGroupDiv.appendChild(analyseButton);
    const listDiv = document.createElement("div");
    const endpoint = "/download?&file=" + downloadOptions["files"][i];
    downloadButton.textContent = "Download";
    downloadButton.addEventListener("click", (event) => {
      event.preventDefault();
      console.log("Downloading", endpoint);
      downloadFile(endpoint);
    });

    analyseButton.addEventListener("click", (event) => {
      event.preventDefault();
      console.log("Downloading", endpoint, "for analysis");
      downloadAndAnalyse(endpoint);
    });
    analyseButton.textContent = "Analyse";
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

function downloadAndAnalyse(endpoint) {

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
  const measurementData = new Float32Array(downloadedData.slice(8, downloadedData.length).buffer);
  setProgressbar(100, "Parsing Data");

  console.log("Download finished... starting analysis");
  setProgressbar(0, "No work pending");
  // Create measurement Analyser
  measurementAnalyser = new MeasurementAnalyser(measurementData);

}

function setProgressbar(value, label) {
  const progressbar = document.getElementById("progressbar");
  const progressbarLabel = document.getElementById("progressbarLabel");
  progressbarLabel.textContent = label;
  progressbar.style.width = `${value}%`;
  progressbar.setAttribute("aria-valuenow", value);
  progressbar.textContent = `${Math.round(value)}%`;
}

class MeasurementAnalyser {
  constructor(time_sequence) {
    this.sequence = time_sequence;


    this.startIdx = 0;
    this.endIdx = time_sequence.length - 1;
    this.spectrogram = new Spectrogram(1024, time_sequence.length * 20 / 1000);
    this.setFFTWindowSize(1024);
    // TODO make sure the number is correct
    this.durationSeconds = linspace(0, time_sequence.length * 20 / 1000, this.spectrogram.width);
    this.totalSoundPressureLevels = new Float32Array(this.durationSeconds.length);
    this.previewSelectedRange = this.previewSelectedRange.bind(this);
    this.onAnalyseSelectedRange = this.onAnalyseSelectedRange.bind(this);
    this.initAnalysisRangeSelector();
    // Run intial analysis
    this.analyse(this.startIdx, this.endIdx);
  }

  async analyse(startIdx, endIdx) {
    if (typeof startIdx == "undefined") { startIdx = this.startIdx; }
    if (typeof endIdx == "undefined") { endIdx = this.endIdx; }
    const samplesToAnalyse = Math.max(endIdx - startIdx, 0);
    const stride = samplesToAnalyse / this.spectrogram.width;
    // center the fft window at each selected sapmle and compute a spectrum
    const analyseNextWindow = (i) => {
      if (i < this.spectrogram.width) {
        const progress = 100 * (i * stride) / (endIdx - startIdx);
        setProgressbar(progress, "Anayzing");

        // index of center measurement
        const sample_idx = startIdx + Math.round(i * stride);
        // span window around that center of sample_idx
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
        this.spectrogram.render();
        // set sound pressure level value in chart
        this.setSoundpressure(i, spectrum);

        setTimeout(() => {
          // give controll back to event loop to draw the ui elements
          // then analyse the next window
          analyseNextWindow(i + 1);
        }, 0);
      } else {
        console.log('All windows analysed');
      }
    }
    analyseNextWindow(0);

    this.startIdx = startIdx;
    this.endIdx = endIdx;
    setProgressbar(0, "No work pending");
  }

  setSoundpressure(index, spectrum) {
    const frequencies = linspace(0, 25, spectrum.length);
    const totalSoundPressureLevel = computeTotalDBG(frequencies, spectrum);
    this.totalSoundPressureLevels[index] = totalSoundPressureLevel;

    // TODO don't always do this it might be quite slow
    const chart_data = [];
    for (let i = 0; i < this.durationSeconds.length; i++) {
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
  }

  initAnalysisRangeSelector() {
    fromSlider.min = 0;
    fromSlider.max = this.sequence.length - 1;
    fromSlider.value = 0;
    fromInput.min = 0;
    fromInput.max = this.sequence.length - 1;
    fromInput.value = 0;
    toSlider.min = 0;
    toSlider.max = this.sequence.length - 1;
    toSlider.value = this.sequence.length - 1;
    toInput.min = 0;
    toInput.max = this.sequence.length - 1;
    toInput.value = this.sequence.length - 1;
    controlFromSlider(fromSlider, toSlider, fromInput);
    controlToSlider(fromSlider, toSlider, toInput);

    fromSlider.addEventListener("input", this.previewSelectedRange);
    toSlider.addEventListener("input", this.previewSelectedRange);
    fromInput.addEventListener("input", this.previewSelectedRange);
    toInput.addEventListener("input", this.previewSelectedRange);
    // TODO add the confirm listener here
    analyseRangeButton.addEventListener("click", this.onAnalyseSelectedRange);
    analyseRangeButton.disabled = false;
  }

  previewSelectedRange() {
    const from = parseInt(fromSlider.value, 10);
    const to = parseInt(toSlider.value, 10);

    this.spectrogram.previewSelectedRange(this.startIdx, this.endIdx, from, to);
  }

  onAnalyseSelectedRange() {
    const from = parseInt(fromSlider.value);
    const to = parseInt(toSlider.value);
    console.log("Analysing selected range from index", from, "to index", to);

    this.startIdx = from;
    this.endIdx = from;

    this.spectrogram.previewSelectedRange(this.startIdx, this.endIdx, this.startIdx, this.endIdx);
    this.analyse(from, to);
  }
}


class Spectrogram {
  constructor(fft_window_size, total_duration) {
    this.total_frequency_steps = fft_window_size / 2;
    this.total_duration = total_duration;

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
    // initialize webg spectrogram
    this.initWebgl();

    // initialize the texture with zeros
    const width = document.getElementById("SpectrogramContainer").offsetWidth;
    this.setWidth(width);

    // draw Colormap
    this.drawColormap();
    this.render();

    this.previewSelectedRange = this.previewSelectedRange.bind(this);
  }

  previewSelectedRange(startIdx, endIdx, from, to) {
    this.startIdx = startIdx;
    this.endIdx = endIdx;
    this.total_duration = (endIdx - startIdx) * 20 / 1000;
    this.previewRangeFrom = from;
    this.previewRangeTo = to;

    this.renderLabels();
  }

  drawColormap() {
    const colormapCanvas = document.getElementById("colormap");
    const ctx = colormapCanvas.getContext("2d");
    ctx.clearRect(0, 0, colormapCanvas.width, colormapCanvas.height);

    const padding = 50;
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

    // Draw labels
    const unit = "db(G)";
    // Fixed value range for db(G)
    const min = 0;
    const max = 100;

    for (const [i, x] of xs.entries()) {
      const weight = i / (numXLabels - 1);
      const value = (1 - weight) * min + weight * max;
      const label = `${value.toFixed(1)} ${unit}`;
      const labelWidth = ctx.measureText(label).width;
      ctx.fillStyle = "black";
      ctx.font = "14px Arial";
      ctx.fillText(label, x - labelWidth / 2, colormapCanvas.height - 10);
    }
  }

  initWebgl() {
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
// minimum and maximum value for normalization
uniform float uMinDbG;
uniform float uMaxDbG;

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
  fragCoord.y = shape.y - fragCoord.y - 1.f;
  
  vec2 texCoord = fragCoord / shape;
  float frequency_steps = 25.0 / (shape.y + 1.0);
  float frequency = frequency_steps * (1.0 + fragCoord.y);

  // load texel coordinate for the current position
  float value = texture(uTexture, texCoord).r;
  
  value = computeDBG(value, frequency);
  float minValue = uMinDbG;
  float maxValue = uMaxDbG;
  // Make sure min value is not -inf if an amplitude ever really gets 0
  value = clamp(value, minValue, maxValue);
  
  // Normalize spectrogram to [0, 1]
  value = (value - minValue) / (maxValue - minValue);
  // get color
  vec3 color = rainbowColor(value);
  // vec3 color = heatmapColor(value);
  fragColor = vec4(color, 1.0);
  }
`
    this.vertexShader = compileShader(this.gl, vertexShaderSource, this.gl.VERTEX_SHADER);
    this.fragmentShader = compileShader(
      this.gl,
      fragmentShaderSource,
      this.gl.FRAGMENT_SHADER,
    );
    this.shaderProgram = this.gl.createProgram();
    this.gl.attachShader(this.shaderProgram, this.vertexShader);
    this.gl.attachShader(this.shaderProgram, this.fragmentShader);
    this.gl.linkProgram(this.shaderProgram);

    //check if linking program was successful
    if (!this.gl.getProgramParameter(this.shaderProgram, this.gl.LINK_STATUS)) {
      console.error(
        "Error linking program:",
        this.gl.getProgramInfoLog(this.shaderProgram),
      );
    }
    // create a rectangle
    const positionBuffer = this.gl.createBuffer();
    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, positionBuffer);
    const positions = new Float32Array([
      -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1,
    ]);
    this.gl.bufferData(this.gl.ARRAY_BUFFER, positions, this.gl.STATIC_DRAW);

    // Set up attribute for vertex positions
    const positionLocation = this.gl.getAttribLocation(this.shaderProgram, "aPosition");
    this.gl.enableVertexAttribArray(positionLocation);
    this.gl.vertexAttribPointer(positionLocation, 2, this.gl.FLOAT, false, 0, 0);
  }

  render() {
    // render the webgl part
    this.gl.clear(this.gl.COLOR_BUFFER_BIT);
    this.gl.useProgram(this.shaderProgram);

    this.gl.uniform1f(
      this.gl.getUniformLocation(this.shaderProgram, "uMinDbG"),
      0.0,
    );
    this.gl.uniform1f(
      this.gl.getUniformLocation(this.shaderProgram, "uMaxDbG"),
      100.0,
    );

    // bind texture
    this.gl.activeTexture(this.gl.TEXTURE0);
    this.gl.bindTexture(this.gl.TEXTURE_2D, this.texture);
    this.gl.drawArrays(this.gl.TRIANGLES, 0, 6);

    // render the labels on top
    this.renderLabels();
  }

  renderLabels() {
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
      const x = labelCanvas.width * (i / numXLabels);
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, labelCanvas.height);
      ctx.strokeStyle = "rgba(0, 0, 0, 0.5)";
      ctx.lineWidth = 1;
      ctx.stroke();
    }

    // draw y-axis labels (frequencies)
    const freqMin = 0;
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
    const startTime = 0;
    const endTime = this.total_duration;
    for (let i = 0; i <= numXLabels; i++) {
      const x = labelCanvas.width * (i / numXLabels);
      const time = startTime -
        (i * (startTime - endTime)) / numXLabels;
      const timeString = `${time.toFixed(1)} sec`;
      const timeStringWidth = ctx.measureText(timeString).width;
      ctx.fillStyle = "black";
      ctx.font = "14px Arial";
      ctx.fillText(timeString, x - timeStringWidth, labelCanvas.height - 10);
    }

    // draw preview ranges
    if (this.previewRangeFrom > this.startIdx) {
      const idx2pixelScale = labelCanvas.width / (this.endIdx - this.startIdx);
      const width = (this.previewRangeFrom - this.startIdx) * idx2pixelScale;
      ctx.fillStyle = "rgba(200, 0, 0, 0.5)";
      ctx.fillRect(0, 0, width, labelCanvas.height);
    }
    if (this.previewRangeTo < this.endIdx) {
      const idx2pixelScale = labelCanvas.width / (this.endIdx - this.startIdx);
      const start = (this.endIdx - this.previewRangeTo) * idx2pixelScale;
      ctx.fillStyle = "rgba(200, 0, 0, 0.5)";
      ctx.fillRect(labelCanvas.width - start, 0, labelCanvas.width, labelCanvas.height);
    }
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
    const webglCanvas = document.getElementById("webglCanvas");
    const labelCanvas = document.getElementById("labelCanvas");
    webglCanvas.height = labelCanvas.height = this.total_frequency_steps;
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
      this.total_frequency_steps,
      0,
      this.gl.RED,
      this.gl.FLOAT,
      data,
    );
  }

  setWidth(newWidth) {
    if (this.width == newWidth) {
      return;
    }
    this.width = newWidth;

    // Resize the canvases
    const webglCanvas = document.getElementById("webglCanvas");
    const labelCanvas = document.getElementById("labelCanvas");
    const colormapCanvas = document.getElementById("colormap");
    const container = document.getElementById("SpectrogramDiv");
    webglCanvas.width = labelCanvas.width = this.width;
    webglCanvas.height = labelCanvas.height = this.total_frequency_steps;
    this.gl.viewport(0, 0, webglCanvas.width, webglCanvas.height);
    colormapCanvas.width = this.width;
    colormapCanvas.height = 50;
    container.style.height = `${this.total_frequency_steps}px`;

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

    this.drawColormap();
    this.render();
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

// webgl stuff

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

// colormap 
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


// Range slider from https://medium.com/@predragdavidovic10/native-dual-range-slider-html-css-javascript-91e778134816
function controlFromInput(fromSlider, fromInput, toInput, controlSlider) {
  const [from, to] = getParsed(fromInput, toInput);
  fillSlider(fromInput, toInput, '#C6C6C6', '#25daa5', controlSlider);
  if (from > to) {
    fromSlider.value = to;
    fromInput.value = to;
  } else {
    fromSlider.value = from;
  }
}

function controlToInput(toSlider, fromInput, toInput, controlSlider) {
  const [from, to] = getParsed(fromInput, toInput);
  fillSlider(fromInput, toInput, '#C6C6C6', '#25daa5', controlSlider);
  setToggleAccessible(toInput);
  if (from <= to) {
    toSlider.value = to;
    toInput.value = to;
  } else {
    toInput.value = from;
  }
}

function controlFromSlider(fromSlider, toSlider, fromInput) {
  const [from, to] = getParsed(fromSlider, toSlider);
  fillSlider(fromSlider, toSlider, '#C6C6C6', '#25daa5', toSlider);
  if (from > to) {
    fromSlider.value = to;
    fromInput.value = to;
  } else {
    fromInput.value = from;
  }
}

function controlToSlider(fromSlider, toSlider, toInput) {
  const [from, to] = getParsed(fromSlider, toSlider);
  fillSlider(fromSlider, toSlider, '#C6C6C6', '#25daa5', toSlider);
  setToggleAccessible(toSlider);
  if (from <= to) {
    toSlider.value = to;
    toInput.value = to;
  } else {
    toInput.value = from;
    toSlider.value = from;
  }
}

function getParsed(currentFrom, currentTo) {
  const from = parseInt(currentFrom.value, 10);
  const to = parseInt(currentTo.value, 10);
  return [from, to];
}

function fillSlider(from, to, sliderColor, rangeColor, controlSlider) {
  const rangeDistance = to.max - to.min;
  const fromPosition = from.value - to.min;
  const toPosition = to.value - to.min;
  controlSlider.style.background = `linear-gradient(
      to right,
      ${sliderColor} 0%,
      ${sliderColor} ${(fromPosition) / (rangeDistance) * 100}%,
      ${rangeColor} ${((fromPosition) / (rangeDistance)) * 100}%,
      ${rangeColor} ${(toPosition) / (rangeDistance) * 100}%, 
      ${sliderColor} ${(toPosition) / (rangeDistance) * 100}%, 
      ${sliderColor} 100%)`;
}

function setToggleAccessible(currentTarget) {
  const toSlider = document.querySelector('#toSlider');
  if (Number(currentTarget.value) <= 0) {
    toSlider.style.zIndex = 2;
  } else {
    toSlider.style.zIndex = 0;
  }
}

fillSlider(fromSlider, toSlider, '#C6C6C6', '#25daa5', toSlider);
setToggleAccessible(toSlider);

fromSlider.oninput = () => controlFromSlider(fromSlider, toSlider, fromInput);
toSlider.oninput = () => controlToSlider(fromSlider, toSlider, toInput);
fromInput.oninput = () => controlFromInput(fromSlider, fromInput, toInput, toSlider);
toInput.oninput = () => controlToInput(toSlider, fromInput, toInput, toSlider);
