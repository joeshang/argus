"use strict";

function MonitoringService(eventSource) {
  eventSource.register(this);
  this.sendDataTimer = 2 * 1000;
  this._monitorEnabled = true;
  this._excludedUrlPatterns = [];
  this._tabSessionMap = {};
  this._tabHostMap = {};
  this._openRequests = {};
}

var updateData = new Map(), timer;

chrome.runtime.onMessage.addListener(contentScriptMessagingOnMessage);

function contentScriptMessagingOnMessage(message, sender, sendResponse) {
  if (!message.requestId) {
    return;
  }

  let requestType;
  //   if (sender.frameId == 0) requestType = ProtocolWebRequestTypes.MainFrame;
  //   else requestType = ProtocolWebRequestTypes.SubFrame;
  let tabId = sender.tab.id;
  updateData.set(tabId, {
    total: message.pageLoadTotalMs,
    network: message.pageLoadNetworkMs,
    render: message.pageLoadRenderMs,
  });
}

MonitoringService.prototype.getBrowser = function () {
  var browserName = "unknown";
  for (const brand of navigator.userAgentData.brands) {
    let brandName = brand.brand;
    if (brandName == "Google Chrome") {
      browserName = "chrome";
      break;
    } else if (brandName == "Microsoft Edge") {
      browserName = "edge";
      break;
    }
  }
  return browserName;
};

MonitoringService.prototype.getOS = function () {
  return navigator.userAgentData.platform;
};

MonitoringService.prototype.isEnabled = function () {
  return this._monitorEnabled;
};

MonitoringService.prototype.addExcludedUrlPattern = function (regExp) {
  this._excludedUrlPatterns.push(regExp);
};

MonitoringService.prototype.toBeExcluded = function (url) {
  for (var i = 0; i < this._excludedUrlPatterns.length; i++) {
    var excludedPattern = this._excludedUrlPatterns[i];
    if (url.search(excludedPattern) >= 0) return true;
  }
  return false;
};

MonitoringService.prototype.shouldBeMonitored = function (requestID) {
  return this._monitorEnabled && requestID in this._openRequests;
};

MonitoringService.prototype.onRequest = function (details) {
  var requestID = details.requestId;
  if (this._monitorEnabled && !this.toBeExcluded(details.url)) {
    this._openRequests[requestID] = { request: details };
  }
};

MonitoringService.prototype.onRequestCompleted = function (details) {
  var requestID = details.requestId;
  if (this.shouldBeMonitored(requestID)) {
    var request = this._openRequests[requestID].request;
    this._archiveRequest(details, request, requestID);
    delete this._openRequests[requestID];
  }
};

MonitoringService.prototype._archiveRequest = function (
  details,
  request,
  reqeustId
) {
  var startTime = request.timeStamp,
    endTime = details.timeStamp,
    duringTime = endTime - startTime,
    statusCode = details.statusCode,
    tabId = "tab-" + details.tabId;

  if (this._tabSessionMap[tabId] === undefined) {
    this._tabSessionMap[tabId] = {
      httpStatusCount2xx: 0,
      httpStatusCount3xx: 0,
      httpStatusCount4xx: 0,
      httpStatusCount5xx: 0,
      requestCount: 0,
      webRequestDurationMs: 0, // duration for host
      pageLoadTotalDurationMs: 0,
      pageLoadNetworkDurationMs: 0,
      pageLoadRenderDurationMs: 0,
    };
  }
  //   var urls = this._tabSessionMap.tabs[tabId].urls;
  //   urls.push(obj);
  //   this._tabSessionMap.tabs[tabId].urls = urls;
  this._tabSessionMap[tabId].requestCount++;
  if (details.type === "main_frame") {
    this._tabSessionMap[tabId].webRequestDurationMs = Math.floor(duringTime);
  }

  if (199 < statusCode && statusCode < 300) {
    this._tabSessionMap[tabId].httpStatusCount2xx++;
  }
  if (299 < statusCode && statusCode < 400) {
    this._tabSessionMap[tabId].httpStatusCount3xx++;
  }
  if (399 < statusCode && statusCode < 500) {
    this._tabSessionMap[tabId].httpStatusCount4xx++;
  }
  if (499 < statusCode && statusCode < 600) {
    this._tabSessionMap[tabId].httpStatusCount5xx++;
  }

  if (this.getBrowser() === "chrome" || this.getBrowser() === "edge") {
    chrome.tabs.executeScript(
      details.tabId,
      { file: "content.js" },
      function () {
        chrome.tabs.sendMessage(
          details.tabId,
          { requestId: reqeustId },
          { frameId: 0 }
        );
      }
    );
  }
};

MonitoringService.prototype.reset = function () {
  this._openRequests = {};
  this._tabSessionMap = {};
};

MonitoringService.prototype.update = function (data) {
  if (data.size === 0) {
    console.log("No need to update page load data");
    return;
  } else {
    for (let id of data.keys()) {
      var tabId = "tab-" + id;
      if (this._tabSessionMap[tabId]) {
        this._tabSessionMap[tabId].pageLoadTotalDurationMs = data.get(id).total;
        this._tabSessionMap[tabId].pageLoadNetworkDurationMs =
          data.get(id).network;
        this._tabSessionMap[tabId].pageLoadRenderDurationMs =
          data.get(id).render;
      }
    }
  }
};

MonitoringService.prototype.query_tab = function () {
  chrome.tabs.query({}, (tabs) => {
    for (const tab of tabs) {
      chrome.webNavigation.getAllFrames(
        {
          tabId: tab.id,
        },
        (frames) => {
          if (chrome.runtime.lastError) {
            return;
          }
          var topFrameProcessId = 0;
          for (var i = 0; i < frames.length; i++) {
            var frame = frames[i];
            // The top-level frame has frame ID 0.
            if (frame.frameId === 0) {
              topFrameProcessId = frame.processId;
              var tabId = "tab-" + tab.id;
              if (this._tabSessionMap[tabId]) {
                var sessionInfo = this._tabSessionMap[tabId];
                this.port.postMessage({
                  browser: this.getBrowser(),
                  OS: this.getOS(),
                  type: "tab",
                  isSubframe: false,
                  url: frame.url,
                  rendererClientId: frame.processId,
                  withMetric: true,
                  httpStatusCount2xx: sessionInfo.httpStatusCount2xx,
                  httpStatusCount3xx: sessionInfo.httpStatusCount3xx,
                  httpStatusCount4xx: sessionInfo.httpStatusCount4xx,
                  httpStatusCount5xx: sessionInfo.httpStatusCount5xx,
                  requestCount: sessionInfo.requestCount,
                  webRequestDurationMs: sessionInfo.webRequestDurationMs,
                  pageLoadTotalDurationMs: sessionInfo.pageLoadTotalDurationMs,
                  pageLoadNetworkDurationMs: sessionInfo.pageLoadNetworkDurationMs,
                  pageLoadRenderDurationMs: sessionInfo.pageLoadRenderDurationMs,
                });
              } else {
                this.port.postMessage({
                  browser: this.getBrowser(),
                  OS: this.getOS(),
                  type: "tab",
                  isSubframe: false,
                  url: frame.url,
                  rendererClientId: frame.processId
                });
              }
            } else if (frame.processId !== topFrameProcessId) {
              this.port.postMessage({
                browser: this.getBrowser(),
                OS: this.getOS(),
                type: "tab",
                isSubframe: true,
                url: frame.url,
                rendererClientId: frame.processId
              });
            }
          }
        }
      );
    }
  });
};


MonitoringService.prototype.sendCollectData = function () {
  this.port = chrome.runtime.connectNative("com.argus.extension.helper");
  this.port.onDisconnect.addListener(function () {
    console.log("Disconnected");
  });
  this.port.onMessage.addListener((response) => {
    console.log(`Received from NativeService: ${JSON.stringify(response)}`);
  });
  if (JSON.stringify(this._tabSessionMap) !== "{}") {
    this.update(updateData);
  }
  this.query_tab();
  timer = setInterval(() => {
    if (JSON.stringify(this._tabSessionMap) !== "{}") {
      this.update(updateData);
    }
    this.query_tab();
  }, this.sendDataTimer);
};

const button = document.querySelector("button");
button.addEventListener("click", async () => {
  clearInterval(timer);
});