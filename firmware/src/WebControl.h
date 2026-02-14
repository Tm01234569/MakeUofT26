#ifndef WebControl_h
#define WebControl_h

#include <Arduino.h>

#if __has_include(<WebServer.h>)
#include <WebServer.h>
#define DAZI_WEBCTRL_HAS_WEBSERVER 1
#endif

#if __has_include(<WebSocketsServer.h>)
#include <WebSocketsServer.h>
#define DAZI_WEBCTRL_HAS_WS 1
#endif

typedef String (*WebControlStatusHandler)();
typedef String (*WebControlConfigGetter)();
typedef bool (*WebControlConfigSetter)(const String& configJson);
typedef bool (*WebControlActionHandler)();
typedef bool (*WebControlPromptHandler)(const String& prompt);
typedef bool (*WebControlModelHandler)(const String& provider, const String& model);
typedef String (*WebControlMemoryGetter)();
typedef bool (*WebControlMemoryClearer)();

class WebControl {
  public:
    WebControl(uint16_t port = 80, uint16_t wsPort = 81);

    bool begin();
    void loop();
    bool available() const;

    void setStatusHandler(WebControlStatusHandler fn);
    void setConfigHandlers(WebControlConfigGetter getFn, WebControlConfigSetter setFn);
    void setActionHandlers(WebControlActionHandler startFn, WebControlActionHandler stopFn);
    void setPromptHandler(WebControlPromptHandler fn);
    void setModelHandler(WebControlModelHandler fn);
    void setMemoryHandlers(WebControlMemoryGetter getFn, WebControlMemoryClearer clearFn);

    void broadcastEvent(const String& eventJson);

  private:
    uint16_t _port;
    uint16_t _wsPort;

    WebControlStatusHandler _statusHandler;
    WebControlConfigGetter _configGet;
    WebControlConfigSetter _configSet;
    WebControlActionHandler _startHandler;
    WebControlActionHandler _stopHandler;
    WebControlPromptHandler _promptHandler;
    WebControlModelHandler _modelHandler;
    WebControlMemoryGetter _memoryGet;
    WebControlMemoryClearer _memoryClear;

#if DAZI_WEBCTRL_HAS_WEBSERVER
    WebServer _server;
#endif
#if DAZI_WEBCTRL_HAS_WS
    WebSocketsServer _ws;
#endif

#if DAZI_WEBCTRL_HAS_WEBSERVER
    void _registerRoutes();
#endif
};

#endif
