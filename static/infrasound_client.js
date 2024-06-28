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
    type: "area",
    renderTo: "infrasound-spectrum",
    animation: false
  },
  title: { text: "Spektrum" },
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
  xAxis: { type: 'linear', title: { text: "Frequenz [Hz]" } },
  yAxis: {
    title: { text: "Amplitude [Pa]" }
  },
  credits: { enabled: false }
});


var movingSequence = [];
var movingChartData = [];
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

function updateCharts(dataJson) {
  timeArrayMS = dataJson["ms"];
  preassurePA = dataJson["preassure"];
  var fft_window = 2 ** document.getElementById("spectrumRange").value;

  for (let i = 0; i < timeArrayMS.length; i++) {
    var x = timeArrayMS[i];
    var y = preassurePA[i];
    movingSequence.push(y);
    movingChartData.push([x, y]);

    if (chartTimeSeries.series[0].data.length == chartTimeSeriesDuration) {
      chartTimeSeries.series[0].addPoint([x, y], false, true, false);
    } else if (chartTimeSeries.series[0].data.length > chartTimeSeriesDuration) {
      console.log("Setting chart data to", movingChartData.slice(-chartTimeSeriesDuration));
      chartTimeSeries.series[0].setData(movingChartData.slice(-chartTimeSeriesDuration));
    } else {
      chartTimeSeries.series[0].addPoint([x, y], false, false, false);
    }
    chartTimeSeries.update({}, true, false, false);
  }

  movingSequence = movingSequence.slice(-4096);
  var fft_time_sequence = Array.from(movingSequence.slice(-fft_window));
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

  var test = [];
  for (var i = 0; i < spectrum.length; i++) {
    var hz = i * 50.0 / fft_time_sequence.length;
    test[i] = [hz, spectrum[i] ** 0.5 * 2 / fft_time_sequence.length];
  }
  chartSpectrum.series[0].setData(test);
  chartSpectrum.update({}, true, false, false);

}


setInterval(function() {
  var xmlhttp = new XMLHttpRequest();
  xmlhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var measurements = JSON.parse(this.responseText);
      updateCharts(measurements);
    }
  };
  xmlhttp.open("GET", "/measurements", true);
  xmlhttp.send();
}, 200);

document.getElementById("timeSeriesRange").value = chartTimeSeriesDuration;
document.getElementById("timeSeriesRangeIndicator").innerHTML = "Dargestellte Zeitspanne: " + chartTimeSeriesDuration / 50 + " seconds";
// Slider for time duration 
document.getElementById("timeSeriesRange").oninput = function() {
  var val = document.getElementById("timeSeriesRange").value;
  chartTimeSeriesDuration = val;
  document.getElementById("timeSeriesRangeIndicator").innerHTML = "Dargestellte Zeitspanne: " + val / 50 + " seconds";
};

document.getElementById("SpectrumRangeIndicator").innerHTML = "Spektrum Analyse Dauer: " + 2 ** document.getElementById("spectrumRange").value / 50.0 + " seconds (" + 2 ** document.getElementById("spectrumRange").value + " samples)";
document.getElementById("spectrumRange").oninput = function() {
  var val = 2 ** document.getElementById("spectrumRange").value;
  cleanup_pfft();
  initialize_pfft(val);
  document.getElementById("SpectrumRangeIndicator").innerHTML = "Spektrum Analyse Dauer: " + val / 50.0 + " seconds (" + val + " samples)";
};
