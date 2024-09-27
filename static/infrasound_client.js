var start_timestamp = 0;
var ms_between_measurements = 20;

var is_event_listener_running = false;
var number_of_new_measurements = 0;

var computeTotalNoise = null;
var computeSpectrumFromSquaredMagnitudes = null;

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
    title: { text: "Relativer Luftdruck [Pa]" },
    gridLineWidth: 1
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
    gridLineWidth: 1
  },
  credits: { enabled: false }
});



var measurement_buffer = [];
var times_buffer = [];
var index_buffer = [];
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

  var fft_squared_magnitudes = new Float32Array(dataHeap.buffer, dataHeap.byteOffset, timeSequence.length);
  fft_squared_magnitudes = fft_squared_magnitudes.slice(0, fft_squared_magnitudes.length / 2);

  scaled_magnitudes = fft_squared_magnitudes.map(value => 2 * value ** 0.5 / timeSequence.length);

  return scaled_magnitudes;
}

function setTotalNoise(totalValue) {
  var totalValueElement = document.getElementById("totalValue");
  totalValueElement.innerText = totalValue;
}

function updateCharts() {
  // We never show data older than 5 minutes = 15000 samples @ 50 Hz
  measurement_buffer = measurement_buffer.slice(-15000);
  times_buffer = times_buffer.slice(-15000);
  index_buffer = index_buffer.slice(-15000);

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
  var frequencies = Array(spectrum.length);
  for (var i = 0; i < spectrum.length; i++) {
    var hz = i * 50.0 / fft_time_sequence.length;
    frequencies[i] = hz;
  }
  spectrum = computeSpectrumFromSquaredMagnitudes(spectrum, frequencies);
  totalNoise = computeTotalNoise(spectrum, frequencies);
  setTotalNoise(totalNoise);

  var spectrumChartData = [];
  for (let i = 0; i < spectrum.length; i++) {
    spectrumChartData[i] = [frequencies[i], spectrum[i]];
  }
  chartSpectrum.series[0].setData(spectrumChartData, false, false, false);
  chartSpectrum.series[1].setData(spectrumChartData, false, false, false);
  chartSpectrum.update({}, true, false, false);
}

// load the start-timestamp
var start_timestamp_request = new XMLHttpRequest();

start_timestamp_request.onreadystatechange = function() {
  if (start_timestamp_request.readyState == XMLHttpRequest.DONE && start_timestamp_request.status == 200) {
    var responseText = start_timestamp_request.responseText;
    console.log("Initial timestamp: ", responseText);
    start_timestamp = parseInt(responseText);
    console.log("Loading initial measurements...");
    setupEventListener();
  }
};
start_timestamp_request.open(
  "GET",
  "/start_timestamp", true);
start_timestamp_request.send();



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
      message = e.data.split(";");
      if (message.length != 2) {
        console.log("ERROR: message length is", message.length);
        return;
      }
      let index_string = message[0];
      let meassurement_string = message[1];

      let new_index = parseInt(index_string);
      let new_measurement = parseFloat(meassurement_string);
      measurement_buffer.push(new_measurement);
      var new_timestamp;
      if (times_buffer.length == 0) {
        new_timestamp = start_timestamp;
      } else {
        let start_idx = index_buffer[index_buffer.length - 1];
        let time_since_start = (new_index - start_idx) * ms_between_measurements
        new_timestamp = times_buffer[times_buffer.length - 1] + time_since_start;
      }
      times_buffer.push(new_timestamp);
      index_buffer.push(new_index);
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

function computeTotalRMS(frequencies, spectrum) {
  var rms = 0.0;
  rms = spectrum.slice(1).map(value => value ** 2).reduce((sum, value) => sum + value, 0);  // skip the first entry with static content
  rms = rms / (spectrum.length - 1);
  rms = Math.sqrt(rms);
  return rms;
}
computeTotalNoise = computeTotalRMS;

function computeTotalSPL(frequencies, spectrum) {
  var rms = computeTotalRMS(frequencies, spectrum);
  var p_ref = 20e-6;  // Reference value for SPL 20 micro pascal
  var spl = 20 * Math.log10(rms / p_ref);
  return spl;
}

function computeRMSSpectrum(frequencies, spectrum) {
  return spectrum;
}

function computeSPLSpectrum(frequenies, spectrum) {
  var sqrt_2 = Math.sqrt(2);
  var p_ref = 20e-6;  // Reference value for SPL 20 micro pascal
  return spectrum.map(value => 20 * (Math.log10((value / sqrt_2) / p_ref)));
}

function AWeighting(frequency) {
  return 20 * Math.log10(
    (12200 * 12200 * frequency ** 4) /
    ((frequency ** 2 + 20.6 ** 2) * (frequency ** 2) * Math.sqrt((frequency ** 2 + 107.7 ** 2) * (frequency ** 2 + 737.9 ** 2)))
  );
}

function computeDBASpectrum(frequencies, spectrum) {
  var result_spectrum = computeSPLSpectrum(frequencies, spectrum);
  for (let i = 0; i < spectrum.length; i++) {
    result_spectrum[i] = result_spectrum[i] + AWeighting(frequencies[i]);
  }
  return result_spectrum;
}

function computeTotalDBA(frequencies, spectrum) {
  dba_spectrum = computeDBASpectrum(frequencies, spectrum);
  return 10 * Math.log10(dba_spectrum.reduce((sum, value) => sum + 10 ** (value / 10)));
}

function handleAmplitudeUnitSwitch(radio) {
  var choice = radio.id;
  switch (choice) {
    case "usePa":
      var totalTitleLabel = document.getElementById("totalTitleLabel");
      totalTitleLabel.innerText = "Effektivwert des Schalldrucks:";
      var totalUnit = document.getElementById("totalUnit");
      totalUnit.innerText = "Pascal (Pa)";
      var infoRMS = document.getElementById("infoRMS");
      var infoSPL = document.getElementById("infoSPL");
      var infoDBA = document.getElementById("infoDBA");
      infoRMS.style.display = "block";
      infoSPL.style.display = "none";
      infoDBA.style.display = "none";

      computeTotalNoise = computeTotalRMS;
      computeSpectrumFromSquaredMagnitudes = computeRMSSpectrum;

      chartSpectrum.yAxis.title = "Amplitude [Pa]"; 

      break;
    case "useSPL":
      var totalTitleLabel = document.getElementById("totalTitleLabel");
      totalTitleLabel.innerText = "Dauerschallpegel:";
      var totalUnit = document.getElementById("totalUnit");
      totalUnit.innerText = "db(SPL)";
      var infoRMS = document.getElementById("infoRMS");
      var infoSPL = document.getElementById("infoSPL");
      var infoDBA = document.getElementById("infoDBA");
      infoRMS.style.display = "none";
      infoSPL.style.display = "block";
      infoDBA.style.display = "none";


      computeTotalNoise = computeTotalSPL;
      computeSpectrumFromSquaredMagnitudes = computeSPLSpectrum;
      chartSpectrum.yAxis.title = "Schallpegel [dB(SPL)]"; 
      break;
    case "useDbA":
      var totalTitleLabel = document.getElementById("totalTitleLabel");
      totalTitleLabel.innerText = "A-bewerteter Schallpegel";
      var totalUnit = document.getElementById("totalUnit");
      totalUnit.innerText = "db(A)";
      var infoRMS = document.getElementById("infoRMS");
      var infoSPL = document.getElementById("infoSPL");
      var infoDBA = document.getElementById("infoDBA");
      infoRMS.style.display = "none";
      infoSPL.style.display = "none";
      infoDBA.style.display = "block";

      computeTotalNoise = computeTotalDBA;
      computeSpectrumFromSquaredMagnitudes = computeDBASpectrum;
      chartSpectrum.yAxis.title = "Schallpegel [dB(A)]"; 
      //todo
      break;
    default:
      console.log("WARNING: got unexpected choice for amplitude unit", choice);
  }
}
