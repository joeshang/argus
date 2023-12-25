"use strict";

document.addEventListener("DOMContentLoaded", function(event) {
	var bootstrapper = new Bootstrapper();
});

function Bootstrapper() {
	var eventSource = new ChromeEventSource(chrome);
	eventSource.collectRequests();
	eventSource.collectRequestCompletions();

	var monitoringService = new MonitoringService(eventSource);
	monitoringService.addExcludedUrlPattern(new RegExp("google\.(.+)/_/chrome/newtab", "i"));
	monitoringService.sendCollectData();
}