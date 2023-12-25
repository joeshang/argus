"use strict";
function EventSource() {
	this._observers = [];
}

EventSource.prototype.register = function(observer) {
	this._observers.push(observer);
}

EventSource.prototype.notifyForRequest = function(details) {
	for(var i = 0; i < this._observers.length; i++) {
		var observer = this._observers[i];
		observer.onRequest(details);
	}
}

EventSource.prototype.notifyForRequestComplete = function(details) {
	for(var i = 0; i < this._observers.length; i++) {
		var observer = this._observers[i];
		observer.onRequestCompleted(details);
	}
}

function ChromeEventSource(browserAPI) {
	this.browserAPI = browserAPI;

	EventSource.call(this);
}

ChromeEventSource.prototype = Object.create(EventSource.prototype);

ChromeEventSource.prototype.collectRequests = function() {
	var eventSource = this;
	this.browserAPI.webRequest.onBeforeRequest.addListener(
		function(details) {
			// var httpRequest = eventSource.buildHttpRequest(details);
			eventSource.notifyForRequest(details);
		},
		{urls: ["<all_urls>"]},
		['requestBody']
	);
}

ChromeEventSource.prototype.collectRequestCompletions = function() {
	var eventSource = this;
	this.browserAPI.webRequest.onCompleted.addListener(
		function(details) {
			eventSource.notifyForRequestComplete(details);
		},
		{urls: ["<all_urls>"]}
	);
}