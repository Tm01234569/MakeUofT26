#include "WebControl.h"
#include <ArduinoJson.h>

WebControl::WebControl(uint16_t port, uint16_t wsPort)
  : _port(port),
    _wsPort(wsPort),
    _statusHandler(nullptr),
    _configGet(nullptr),
    _configSet(nullptr),
    _startHandler(nullptr),
    _stopHandler(nullptr),
    _promptHandler(nullptr),
    _modelHandler(nullptr),
    _memoryGet(nullptr),
    _memoryClear(nullptr)
#if DAZI_WEBCTRL_HAS_WEBSERVER
    , _server(port)
#endif
#if DAZI_WEBCTRL_HAS_WS
    , _ws(wsPort)
#endif
{}

bool WebControl::available() const {
#if DAZI_WEBCTRL_HAS_WEBSERVER
  return true;
#else
  return false;
#endif
}

bool WebControl::begin() {
#if !DAZI_WEBCTRL_HAS_WEBSERVER
  Serial.println("[WebControl] WebServer.h missing. Web control disabled.");
  return false;
#else
  _registerRoutes();
  _server.begin();
#if DAZI_WEBCTRL_HAS_WS
  _ws.begin();
#endif
  return true;
#endif
}

void WebControl::loop() {
#if DAZI_WEBCTRL_HAS_WEBSERVER
  _server.handleClient();
#endif
#if DAZI_WEBCTRL_HAS_WS
  _ws.loop();
#endif
}

void WebControl::setStatusHandler(WebControlStatusHandler fn) { _statusHandler = fn; }
void WebControl::setConfigHandlers(WebControlConfigGetter getFn, WebControlConfigSetter setFn) { _configGet = getFn; _configSet = setFn; }
void WebControl::setActionHandlers(WebControlActionHandler startFn, WebControlActionHandler stopFn) { _startHandler = startFn; _stopHandler = stopFn; }
void WebControl::setPromptHandler(WebControlPromptHandler fn) { _promptHandler = fn; }
void WebControl::setModelHandler(WebControlModelHandler fn) { _modelHandler = fn; }
void WebControl::setMemoryHandlers(WebControlMemoryGetter getFn, WebControlMemoryClearer clearFn) { _memoryGet = getFn; _memoryClear = clearFn; }

void WebControl::broadcastEvent(const String& eventJson) {
#if DAZI_WEBCTRL_HAS_WS
  _ws.broadcastTXT(eventJson);
#else
  (void)eventJson;
#endif
}

#if DAZI_WEBCTRL_HAS_WEBSERVER
void WebControl::_registerRoutes() {
  _server.on("/", HTTP_GET, [this]() {
    _server.send(200, "text/html", "<html><body><h1>DAZI Control</h1><p>Use /api/* endpoints.</p></body></html>");
  });

  _server.on("/api/status", HTTP_GET, [this]() {
    String body = _statusHandler ? _statusHandler() : "{}";
    _server.send(200, "application/json", body);
  });

  _server.on("/api/config", HTTP_GET, [this]() {
    String body = _configGet ? _configGet() : "{}";
    _server.send(200, "application/json", body);
  });

  _server.on("/api/config", HTTP_POST, [this]() {
    bool ok = _configSet ? _configSet(_server.arg("plain")) : false;
    _server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  _server.on("/api/start", HTTP_POST, [this]() {
    bool ok = _startHandler ? _startHandler() : false;
    _server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  _server.on("/api/stop", HTTP_POST, [this]() {
    bool ok = _stopHandler ? _stopHandler() : false;
    _server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  _server.on("/api/prompt", HTTP_POST, [this]() {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
      _server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_json\"}");
      return;
    }

    String prompt = doc["prompt"] | "";
    bool ok = _promptHandler ? _promptHandler(prompt) : false;
    _server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  _server.on("/api/model", HTTP_POST, [this]() {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
      _server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_json\"}");
      return;
    }

    String provider = doc["provider"] | "";
    String model = doc["model"] | "";
    bool ok = _modelHandler ? _modelHandler(provider, model) : false;
    _server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  _server.on("/api/memory", HTTP_GET, [this]() {
    String body = _memoryGet ? _memoryGet() : "{\"items\":[]}";
    _server.send(200, "application/json", body);
  });

  _server.on("/api/memory", HTTP_DELETE, [this]() {
    bool ok = _memoryClear ? _memoryClear() : false;
    _server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
}
#endif
