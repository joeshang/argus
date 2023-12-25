"use strict";

(function () {

var uABrowserObject;
if (typeof browser !== 'undefined')
{
   var isFirefox = true;
   uABrowserObject = browser;
}
else if (window.chrome)
{
   var isChrome = true;
   uABrowserObject = window.chrome;
}
else
{
   throw "Browser detection: unsupported browser: " + window.navigator.userAgent;
}

var requestId = 0;

uABrowserObject.runtime.onMessage.addListener (function (message, sender, sendResponse)
{
   if (requestId > 0)
      return;

      requestId = message.requestId;

      getPageLoadTimings ();
});


if (document.readyState == "complete")
{
   getPageLoadTimings ();
}
else
{
   window.addEventListener ("load", getPageLoadTimings);
}

function getPageLoadTimings ()
{
   if (! requestId)
      return;
   if (document.readyState != "complete")
      return;

   setTimeout (function()
   {
      var timingInfo = performance.timing;

      if (timingInfo.loadEventEnd == 0)
      {
         return;
      }

      var message = { requestId: requestId };
      message.startTimeMs = timingInfo.navigationStart;

      message.pageLoadTotalMs = timingInfo.loadEventEnd - timingInfo.navigationStart;

      var networkStartMs = timingInfo.redirectStart == 0 ? timingInfo.fetchStart : timingInfo.redirectStart;
      message.pageLoadNetworkMs = timingInfo.responseEnd - networkStartMs;
      if (message.pageLoadNetworkMs <= 0)
      {
         return;
      }

      message.pageLoadRenderMs = timingInfo.loadEventEnd - timingInfo.responseEnd;

      uABrowserObject.runtime.sendMessage (message);

   }, 0);
}
} ());