var start_timestamp = 0;
var ms_between_measurements = 20;

var is_event_listener_running = false;
var number_of_new_measurements = 0;


pfft_module = null;
pffft().then(async function(Module) {
  if (true) console.log("PFFFT Module initialized");
  pffft_module = Module;
});
var chartTimeSeriesDuration = 30 * 50;
var chartTimeSeries = new Highcharts.Chart({
  chart: {
    renderTo: "infrasound-time-serie",
    animation: false
  },
  title: { text: "Zeit Serie" },
  series: [{
    showInLegend: false,
    data: []
  }],
  plotOptions: {
    line: {
      dataLabels: { enabled: false },
    },
    series: {
      color: '#059e8a',
    },
  },
  xAxis: {
    type: 'datetime',
    dateTimeLabelFormats: { second: "%H:%M:%S" }
  },
  yAxis: {
    title: { text: "Relativer Luftdruck [Pa]" }
  },
  credits: { enabled: false },
  time: {
    timezone: "Europe/Berlin",
  }
});

var chartSpectrum = new Highcharts.Chart({
  chart: {
    renderTo: "infrasound-spectrum",
    animation: false
  },
  title: { text: "Spektrum" },
  series: [{
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
      shadow: false
    },
    series: {
      color: '#059e8a',
    },
  },
  xAxis: {
    title: { text: "Frequenz [Hz]" },
    type: 'linear'
  },
  yAxis: {
    title: { text: "Amplitude [Pa]" },
  },
  credits: { enabled: false }
});



var measurement_buffer = [];
var times_buffer = [];
var pffft_runner = null;
var dataPtr = null;
var dataHeap = null;

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
  var audio_block_size = fft_window;
  var bytes_per_element = 4;
  var nDataBytes = audio_block_size * bytes_per_element;

  pffft_runner = pffft_module._pffft_runner_new(audio_block_size, bytes_per_element);

  dataPtr = pffft_module._malloc(nDataBytes);
  dataHeap = new Uint8Array(pffft_module.HEAPU8.buffer, dataPtr, nDataBytes);
}

function fourier_transform(timeSequence) {
  var buffer = new Float32Array(timeSequence);

  // Copy data to Emscripten heap (directly accessed from Module.HEAPU8)
  dataHeap.set(new Uint8Array(buffer.buffer));

  // Call function and get result
  pffft_module._pffft_runner_transform_magnitudes(pffft_runner, dataHeap.byteOffset);

  var fft_result = new Float32Array(dataHeap.buffer, dataHeap.byteOffset, timeSequence.length);
  fft_result = fft_result.slice(0, fft_result.length / 2);

  return fft_result;
}

function addSpinners() {
  // Spinner for time series
  var time_spinner = document.createElement("div");
  var time_spinner_hidden = document.createElement("span");
  time_spinner.classList.add("spinner-border");
  time_spinner.classList.add("text-primary");
  time_spinner_hidden.classList.add("visually-hidden");
  time_spinner_hidden.innerText = "Loading...";
  time_spinner.appendChild(time_spinner_hidden);
  // Spinner for time spectrum
  var spectrum_spinner = document.createElement("div");
  var spectrum_spinner_hidden = document.createElement("span");
  spectrum_spinner.classList.add("spinner-border");
  spectrum_spinner.classList.add("text-primary");
  spectrum_spinner_hidden.classList.add("visually-hidden");
  spectrum_spinner_hidden.innerText = "Loading...";
  spectrum_spinner.appendChild(spectrum_spinner_hidden);

  // add to page
  var time_container = document.getElementById("infrasound-time-serie");
  time_container.appendChild(time_spinner);
  var spectrum_container = document.getElementById("infrasound-spectrum");
  spectrum_container.appendChild(spectrum_spinner);
}

function removeSpinners() {
  var spinner_elements = document.getElementsByClassName("spinner-border");
  while (spinner_elements.length > 0) {
    spinner_elements[0].remove();
  }
}

function updateCharts() {
  // We never show data older than 5 minutes = 15000 samples @ 50 Hz
  measurement_buffer = measurement_buffer.slice(-15000);
  times_buffer = times_buffer.slice(-15000);

  // Update data in time chart
  chart_data_time = times_buffer.slice(-chartTimeSeriesDuration);
  chart_data_preassure = measurement_buffer.slice(-chartTimeSeriesDuration);
  chart_data = chart_data_time.map(function(val, idx) { return [val, chart_data_preassure[idx]] });
  chartTimeSeries.series[0].setData(chart_data, false, false, false);
  chartTimeSeries.update({}, true, false, false);

  var fft_window = 2 ** document.getElementById("spectrumRange").value;
  var fft_time_sequence = Array.from(measurement_buffer.slice(-fft_window));
  if (fft_time_sequence.length < fft_window) {
    // pad up with zeros
    var padding = Array(fft_window - fft_time_sequence.length).fill(0);
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

  var spectrum = fourier_transform(fft_time_sequence);

  var spectrumChartData = [];
  for (var i = 0; i < spectrum.length; i++) {
    var hz = i * 50.0 / fft_time_sequence.length;
    spectrumChartData[i] = [hz, spectrum[i] ** 0.5 * 2 / fft_time_sequence.length];
  }
  chartSpectrum.series[0].setData(spectrumChartData, false, false, false);
  chartSpectrum.series[1].setData(spectrumChartData, false, false, false);
  chartSpectrum.update({}, true, false, false);
}

function load_initial_sensor_data(xmlhttp) {
  if (xmlhttp.readyState == XMLHttpRequest.DONE && xmlhttp.status == 200) {

    var measurements = JSON.parse(xmlhttp.responseText);
    var next_start_idx = measurements["next_start_idx"];

    // If the sensor ran a while the start time stamp will be further back than
    // the maximum number of measurements we load from the sensor, thus it will be off.
    // However thenext start_idx tells us how many measurements have been reccorded in total, so we can use it to compute
    // the start time of the sequence that was loaded
    start_timestamp = start_timestamp + (next_start_idx - 1) * ms_between_measurements - measurements["preassure"].length * ms_between_measurements;

    for (let i = 0; i < measurements["preassure"].length; i++) {
      var new_timestamp;
      if (times_buffer.length == 0) {
        new_timestamp = start_timestamp;
      } else {
        new_timestamp = times_buffer[times_buffer.length - 1] + ms_between_measurements;
      }
      times_buffer.push(new_timestamp);
      measurement_buffer.push(measurements["preassure"][i]);
    }
    removeSpinners();
    updateCharts();
    setupEventListener();
  }
}

// load the start-timestamp
var start_timestamp_request = new XMLHttpRequest();

start_timestamp_request.onreadystatechange = function() {
  if (start_timestamp_request.readyState == XMLHttpRequest.DONE && start_timestamp_request.status == 200) {
    var responseText = start_timestamp_request.responseText;
    console.log("Initial timestamp: ", responseText);
    start_timestamp = parseInt(responseText);
    console.log("Loading initial measurements...");
    load_initail_measurements();
  }
};
start_timestamp_request.open(
  "GET",
  "/start_timestamp", true);
start_timestamp_request.send();


// initially load all available sensor data
function load_initail_measurements() {
  var initial_sensor_data_request = new XMLHttpRequest();
  initial_sensor_data_request.onreadystatechange = function() {
    if (initial_sensor_data_request.readyState == XMLHttpRequest.DONE && initial_sensor_data_request.status == 200) {
      console.log("initial measurements were loaded")
      load_initial_sensor_data(initial_sensor_data_request);
    }
  }
  initial_sensor_data_request.open(
    "GET",
    "/measurements?&start_with_idx=0&max_length=15000"
  );
  initial_sensor_data_request.send();
  addSpinners();
}


function setupEventListener() {
  // setup event listener for new measurements
  console.log("setupEventListener");
  if (!!window.EventSource) {
    console.log("Creating EventSource");
    var source = new EventSource("/measurement_events");

    source.addEventListener("open", function(e) {
      console.log("Events connected");
    }, false);

    source.addEventListener("error", function(e) {
      if (e.target.readyState != EventSource.OPEN) {
        console.log("Events Disconnected");
      }
    }, false);

    source.addEventListener("measurement", function(e) {
      console.log("meassurement event", e.data);
      var index_string = e.data.substring(0,10);
      var measurement_string = e.data.substring(10, 21);
      // TODO continue here
      
      var new_measurement = parseFloat(e.data);
      measurement_buffer.push(new_measurement);
      var new_timestamp;
      if (times_buffer.length == 0) {
        new_timestamp = start_timestamp;
      } else {
        new_timestamp = times_buffer[times_buffer.length - 1] + ms_between_measurements;
      }
      times_buffer.push(new_timestamp);
      number_of_new_measurements += 1;
      if (number_of_new_measurements > 10) {
        updateCharts();
        number_of_new_measurements = 0;
      }
    }, false);
  }
  is_event_listener_running = true;
}


// Time Series range
document.getElementById("timeSeriesRange").value = chartTimeSeriesDuration;
document.getElementById("timeSeriesRangeIndicator").innerHTML = "Dargestellte Zeitspanne: " + chartTimeSeriesDuration / 50 + " seconds";
// Slider for time duration 
document.getElementById("timeSeriesRange").oninput = function() {
  var val = document.getElementById("timeSeriesRange").value;
  chartTimeSeriesDuration = val;
  document.getElementById("timeSeriesRangeIndicator").innerHTML = "Dargestellte Zeitspanne: " + val / 50 + " seconds";
};

// Spectrum range
document.getElementById("SpectrumRangeIndicator").innerHTML = "Spektrum Analyse Dauer: " + 2 ** document.getElementById("spectrumRange").value / 50.0 + " seconds (" + 2 ** document.getElementById("spectrumRange").value + " samples)";
document.getElementById("spectrumRange").oninput = function() {
  var val = 2 ** document.getElementById("spectrumRange").value;
  cleanup_pfft();
  initialize_pfft(val);
  document.getElementById("SpectrumRangeIndicator").innerHTML = "Spektrum Analyse Dauer: " + val / 50.0 + " seconds (" + val + " samples)";
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

addSpinners();
