function showDownloadOptions(downloadOptions) {
  downloadList = document.getElementById("download-list");
  for (let i = 0; i < downloadOptions["files"].length; i++) {
    var downloadElement = document.createElement("a");
    var endpoint = "/download?&file=" + downloadOptions["files"][i];
    downloadElement.setAttribute("href",endpoint);
    var linkText = document.createTextNode(downloadOptions["files"][i]);
    downloadElement.appendChild(linkText);
    downloadElement.classList.add("list-group-item");
    downloadElement.classList.add("list-group-item-action");
    downloadList.appendChild(downloadElement);
  }
}



var downloadRequest = new XMLHttpRequest();
downloadRequest.onreadystatechange = function() {
  if (this.readyState == 4 && this.status == 200) {
    var downloadOptions = JSON.parse(this.responseText);
    showDownloadOptions(downloadOptions);
  }
};
downloadRequest.open("GET", "/downloads");
downloadRequest.send();
