#include "renderer/renderer_app.h"

#include <cmath>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "brouk/adblock_cosmetic_script.h"
#include "brouk/adblock_picker_script.h"
#include "brouk/forms_script.h"
#include "brouk/json_viewer_script.h"
#include "cef/internal_scheme.h"
#include "core/devtools_frontend.h"
#include "core/internal_pages.h"
#include "core/page_events.h"
#include "core/password_forms.h"
#include "core/screen_share.h"
#include "include/cef_command_line.h"
#include "include/cef_pack_resources.h"
#include "include/cef_process_message.h"
#include "include/cef_resource_bundle.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

int CurrentProcessId() {
#if defined(_WIN32)
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
}

// Names of the native functions while the page script starts; the script
// removes them from the page's global object right away.
constexpr char kReportFunction[] = "__broukReport";
constexpr char kSetDisplayMediaHandlerFunction[] =
    "__broukSetDisplayMediaHandler";
constexpr char kDevToolsSendFunction[] = "__broukDevToolsSend";

// The embedder object the DevTools frontend expects (006). Chrome's is
// native (blink's DevToolsHost); only sendMessageToEmbedder needs the
// browser, the rest are fixed answers for an embedded, non-hosted frontend.
constexpr char kDevToolsHostScript[] = R"js(
(function () {
  'use strict';
  const send = window.__broukDevToolsSend;
  try { delete window.__broukDevToolsSend; } catch (e) {}
  if (typeof send !== 'function') return;
  const host = Object.freeze({
    sendMessageToEmbedder: (message) => send('embedder', String(message)),
    copyText: (text) => send('copy', String(text ?? '')),
    platform: () => 'mac',
    isHostedMode: () => false,
    zoomFactor: () => 1,
    showContextMenuAtPoint: () => {},
    isolatedFileSystem: () => null,
    upgradeDraggedFileSystemPermissions: () => {},
  });
  // Chromium also attaches its own DevTools bindings to devtools:// pages
  // and installs its native DevToolsHost later; those bindings are not
  // connected to the tab. Keep ours: a later assignment is ignored.
  Object.defineProperty(window, 'DevToolsHost', {
    get: () => host,
    set: () => {},
    configurable: false,
    enumerable: false,
  });
})();
)js";

// Reports blocked popups (window.open returns null when Chromium's popup
// blocker stopped it) and whether the page plays audio: a media element
// playing with sound, or a running Web Audio context. Approximates Chrome's
// tab audio indicator, which CEF does not expose.
//
// getDisplayMedia() is replaced: CEF would always share the whole main
// screen. The browser shows a picker and answers with the chosen source,
// which is then requested with getUserMedia() (allowed once by the browser,
// see core/screen_share.h). Like Chrome, it needs a user gesture; if system
// audio cannot be captured with a screen, the screen is shared without it.
constexpr char kPageScript[] = R"js(
(function () {
  'use strict';
  const report = window.__broukReport;
  const setDisplayMediaHandler = window.__broukSetDisplayMediaHandler;
  try { delete window.__broukReport; } catch (e) {}
  try { delete window.__broukSetDisplayMediaHandler; } catch (e) {}
  if (typeof report !== 'function') return;

  // Through the object: the MediaDevices interface is not on the global
  // object yet when this script runs.
  const devices = navigator.mediaDevices && Object.getPrototypeOf(navigator.mediaDevices);
  if (devices && typeof devices.getDisplayMedia === 'function' &&
      typeof setDisplayMediaHandler === 'function') {
    const getUserMedia = devices.getUserMedia;
    const pending = new Map();
    let nextId = 0;
    const frameRate = (video) => {
      let rate = video && typeof video === 'object' ? video.frameRate : undefined;
      if (rate && typeof rate === 'object') rate = rate.max ?? rate.ideal ?? rate.exact;
      rate = Number(rate);
      return Number.isFinite(rate) && rate > 0 ? Math.min(rate, 60) : 30;
    };
    setDisplayMediaHandler((id, source) => {
      const request = pending.get(id);
      if (!request) return;
      pending.delete(id);
      if (!source) {
        request.reject(new DOMException('Permission denied by user', 'NotAllowedError'));
        return;
      }
      const video = { mandatory: {
        chromeMediaSource: 'desktop', chromeMediaSourceId: source,
        maxWidth: 3840, maxHeight: 2160, maxFrameRate: request.frameRate } };
      const capture = (audio) => getUserMedia.call(request.devices, {
        video, audio: audio ? { mandatory: { chromeMediaSource: 'desktop' } } : false });
      const withAudio = request.audio && source.startsWith('screen:');
      capture(withAudio)
        .catch((e) => (withAudio ? capture(false) : Promise.reject(e)))
        .then(request.resolve, request.reject);
    });
    Object.defineProperty(devices, 'getDisplayMedia', {
      configurable: true, enumerable: true, writable: true,
      value: function getDisplayMedia(options) {
        const opts = options ?? {};
        if (navigator.userActivation && !navigator.userActivation.isActive) {
          return Promise.reject(new DOMException(
              'getDisplayMedia() requires transient activation (user gesture).',
              'InvalidStateError'));
        }
        if (opts.video === false) {
          return Promise.reject(new TypeError('getDisplayMedia(): video must be requested'));
        }
        return new Promise((resolve, reject) => {
          const id = ++nextId;
          pending.set(id, { resolve, reject, devices: this, audio: !!opts.audio,
                            frameRate: frameRate(opts.video) });
          report('display-media', id + (opts.audio ? ':audio' : ''));
        });
      },
    });
  }

  const originalOpen = window.open;
  window.open = function open(url, ...rest) {
    const result = originalOpen.call(this, url, ...rest);
    if (result === null) {
      try {
        report('popup-blocked', new URL(String(url ?? ''), document.baseURI).href);
      } catch (e) {}
    }
    return result;
  };

  const media = new Set();
  const contexts = new Set();
  let audible = false;
  let timer = 0;
  const playing = (m) => !m.paused && !m.ended && !m.muted && m.volume > 0;
  const update = () => {
    timer = 0;
    let now = false;
    for (const m of media) {
      if (playing(m)) { now = true; } else if (m.paused || m.ended) { media.delete(m); }
    }
    for (const c of contexts) {
      if (c.state === 'running') { now = true; } else if (c.state === 'closed') { contexts.delete(c); }
    }
    if (now !== audible) {
      audible = now;
      report(now ? 'audible' : 'inaudible', '');
    }
  };
  const schedule = () => { if (!timer) timer = setTimeout(update, 250); };
  const track = (m) => { media.add(m); schedule(); };
  for (const type of ['play', 'playing', 'pause', 'ended', 'volumechange', 'emptied']) {
    document.addEventListener(type, (e) => {
      if (e.target instanceof HTMLMediaElement) track(e.target);
    }, true);
  }
  // Elements not in the document (new Audio()) fire no document events.
  const play = HTMLMediaElement.prototype.play;
  HTMLMediaElement.prototype.play = function (...args) {
    for (const type of ['pause', 'ended', 'volumechange']) {
      this.addEventListener(type, schedule);
    }
    track(this);
    return play.apply(this, args);
  };
  for (const name of ['AudioContext', 'webkitAudioContext']) {
    const Base = window[name];
    if (typeof Base !== 'function') continue;
    window[name] = class extends Base {
      constructor(...args) {
        super(...args);
        contexts.add(this);
        this.addEventListener('statechange', schedule);
        schedule();
      }
    };
  }
  window.addEventListener('pagehide', () => {
    if (audible) { audible = false; report('inaudible', ''); }
  });
})();
)js";

// The native function behind __broukReport(type, payload): forwards the
// event to the browser process, which validates it.
// The cosmetic filtering script's native function (007): ("start",
// receiver) and ("classes", json).
constexpr char kAdblockFunction[] = "__broukAdblock";

class AdblockHandler : public CefV8Handler {
 public:
  explicit AdblockHandler(RendererApp* app) : app_(app) {}
  AdblockHandler(const AdblockHandler&) = delete;
  AdblockHandler& operator=(const AdblockHandler&) = delete;

  bool Execute(const CefString& /*name*/,
               CefRefPtr<CefV8Value> /*object*/,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& /*retval*/,
               CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
    if (!frame || arguments.size() != 2 || !arguments[0]->IsString()) {
      exception = "invalid arguments";
      return true;
    }
    std::string kind = arguments[0]->GetStringValue().ToString();
    if (kind == "start" && arguments[1]->IsFunction()) {
      app_->SetAdblockReceiver(context, arguments[1]);
      frame->SendProcessMessage(
          PID_BROWSER,
          CefProcessMessage::Create(std::string(kAdblockCosmeticRequest)));
    } else if (kind == "classes" && arguments[1]->IsString()) {
      std::string json = arguments[1]->GetStringValue().ToString();
      if (json.size() > kMaxAdblockClassesBytes) {
        return true;
      }
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(std::string(kAdblockClassesRequest));
      message->GetArgumentList()->SetString(0, json);
      frame->SendProcessMessage(PID_BROWSER, message);
    }
    return true;
  }

 private:
  RendererApp* app_;  // Outlives every context.
  IMPLEMENT_REFCOUNTING(AdblockHandler);
};

// The forms script's native function (008): ("start", receiver) registers
// the receiver for fill messages; any other kind goes to the browser with
// its JSON payload, which the browser validates.
constexpr char kFormsFunction[] = "__broukForms";

class FormsHandler : public CefV8Handler {
 public:
  explicit FormsHandler(RendererApp* app) : app_(app) {}
  FormsHandler(const FormsHandler&) = delete;
  FormsHandler& operator=(const FormsHandler&) = delete;

  bool Execute(const CefString& /*name*/,
               CefRefPtr<CefV8Value> /*object*/,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& /*retval*/,
               CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
    if (!frame || arguments.size() != 2 || !arguments[0]->IsString()) {
      exception = "invalid arguments";
      return true;
    }
    std::string kind = arguments[0]->GetStringValue().ToString();
    if (kind == "start") {
      if (arguments[1]->IsFunction()) {
        app_->SetFormsReceiver(context, arguments[1]);
      }
      return true;
    }
    if (!arguments[1]->IsString() || kind.size() > 16) {
      return true;
    }
    std::string json = arguments[1]->GetStringValue().ToString();
    if (json.size() > kMaxFormsMessageBytes) {
      return true;
    }
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(std::string(kFormsMessage));
    message->GetArgumentList()->SetString(0, kind);
    message->GetArgumentList()->SetString(1, json);
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

 private:
  RendererApp* app_;  // Outlives every context.
  IMPLEMENT_REFCOUNTING(FormsHandler);
};

// The element picker's native callback: one picked selector to the
// browser, which accepts it only during a picker the user started.
class PickedHandler : public CefV8Handler {
 public:
  PickedHandler() = default;
  PickedHandler(const PickedHandler&) = delete;
  PickedHandler& operator=(const PickedHandler&) = delete;

  bool Execute(const CefString& /*name*/,
               CefRefPtr<CefV8Value> /*object*/,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& /*retval*/,
               CefString& /*exception*/) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
    if (!frame || arguments.size() != 1 || !arguments[0]->IsString()) {
      return true;
    }
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(std::string(kAdblockPicked));
    message->GetArgumentList()->SetString(0, arguments[0]->GetStringValue());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

 private:
  IMPLEMENT_REFCOUNTING(PickedHandler);
};

class ReportHandler : public CefV8Handler {
 public:
  ReportHandler() = default;
  ReportHandler(const ReportHandler&) = delete;
  ReportHandler& operator=(const ReportHandler&) = delete;

  bool Execute(const CefString& /*name*/,
               CefRefPtr<CefV8Value> /*object*/,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& /*retval*/,
               CefString& exception) override {
    if (arguments.size() != 2 || !arguments[0]->IsString() ||
        !arguments[1]->IsString()) {
      exception = "invalid arguments";
      return true;
    }
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
    if (!frame) {
      return true;
    }
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(std::string(kPageEventMessage));
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetString(0, arguments[0]->GetStringValue());
    args->SetString(1, arguments[1]->GetStringValue());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

 private:
  IMPLEMENT_REFCOUNTING(ReportHandler);
};

// The native function behind __broukSetDisplayMediaHandler(function).
class SetDisplayMediaHandlerFunction : public CefV8Handler {
 public:
  explicit SetDisplayMediaHandlerFunction(CefRefPtr<RendererApp> app)
      : app_(app) {}
  SetDisplayMediaHandlerFunction(const SetDisplayMediaHandlerFunction&) =
      delete;
  SetDisplayMediaHandlerFunction& operator=(
      const SetDisplayMediaHandlerFunction&) = delete;

  bool Execute(const CefString& /*name*/,
               CefRefPtr<CefV8Value> /*object*/,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& /*retval*/,
               CefString& exception) override {
    if (arguments.size() != 1 || !arguments[0]->IsFunction()) {
      exception = "invalid arguments";
      return true;
    }
    app_->SetDisplayMediaHandler(CefV8Context::GetCurrentContext(),
                                 arguments[0]);
    return true;
  }

 private:
  CefRefPtr<RendererApp> app_;
  IMPLEMENT_REFCOUNTING(SetDisplayMediaHandlerFunction);
};

// The native function behind DevToolsHost: forwards (kind, payload) to the
// browser, which accepts it only from its own DevTools frontend.
class DevToolsSendHandler : public CefV8Handler {
 public:
  DevToolsSendHandler() = default;
  DevToolsSendHandler(const DevToolsSendHandler&) = delete;
  DevToolsSendHandler& operator=(const DevToolsSendHandler&) = delete;

  bool Execute(const CefString& /*name*/,
               CefRefPtr<CefV8Value> /*object*/,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& /*retval*/,
               CefString& exception) override {
    if (arguments.size() != 2 || !arguments[0]->IsString() ||
        !arguments[1]->IsString()) {
      exception = "invalid arguments";
      return true;
    }
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
    if (!frame) {
      return true;
    }
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(std::string(kDevToolsHostMessage));
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    args->SetString(0, arguments[0]->GetStringValue());
    args->SetString(1, arguments[1]->GetStringValue());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

 private:
  IMPLEMENT_REFCOUNTING(DevToolsSendHandler);
};

// Sets up the DevTools frontend's embedder API in its main frame (006).
void SetUpDevToolsFrontend(const CefRefPtr<CefFrame>& frame,
                           const CefRefPtr<CefV8Context>& context) {
  if (!frame->IsMain()) {
    return;
  }
  context->GetGlobal()->SetValue(
      kDevToolsSendFunction,
      CefV8Value::CreateFunction(kDevToolsSendFunction,
                                 new DevToolsSendHandler()),
      V8_PROPERTY_ATTRIBUTE_DONTENUM);
  CefRefPtr<CefV8Value> result;
  CefRefPtr<CefV8Exception> exception;
  context->Eval(kDevToolsHostScript, frame->GetURL(), 0, result, exception);
  // The script ships with the frontend in CEF's resources.
  CefRefPtr<CefBinaryValue> script =
      CefResourceBundle::GetGlobal()->GetDataResource(
          DEVTOOLS_COMPATIBILITY_JS);
  if (!script || script->GetSize() == 0) {
    return;
  }
  std::string text(script->GetSize(), '\0');
  script->GetData(text.data(), text.size(), 0);
  // Like Chrome: run it before the frontend's own scripts.
  context->Eval(
      text + "\n//# sourceURL=" + std::string(kDevToolsCompatibilityScriptUrl),
      frame->GetURL(), 0, result, exception);
}

}  // namespace

RendererApp::RendererApp()
    : router_(CefMessageRouterRendererSide::Create(CefMessageRouterConfig())) {}

void RendererApp::OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar) {
  AddInternalScheme(registrar);
}

void RendererApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefV8Context> context) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (frame->IsMain()) {
    // The task manager's Process ID column (020).
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(std::string(kRendererProcessMessage));
    message->GetArgumentList()->SetInt(0, CurrentProcessId());
    frame->SendProcessMessage(PID_BROWSER, message);
  }
  if (IsInternalPageUrl(frame->GetURL().ToString())) {
    // Internal pages get the bridge, web pages never; the browser checks
    // the sender of every message again.
    router_->OnContextCreated(browser, frame, context);
    return;
  }
  if (IsDevToolsFrontendUrl(frame->GetURL().ToString())) {
    SetUpDevToolsFrontend(frame, context);
    return;
  }
  CefRefPtr<CefV8Value> global = context->GetGlobal();
  global->SetValue(
      kReportFunction,
      CefV8Value::CreateFunction(kReportFunction, new ReportHandler()),
      V8_PROPERTY_ATTRIBUTE_DONTENUM);
  global->SetValue(
      kSetDisplayMediaHandlerFunction,
      CefV8Value::CreateFunction(kSetDisplayMediaHandlerFunction,
                                 new SetDisplayMediaHandlerFunction(this)),
      V8_PROPERTY_ATTRIBUTE_DONTENUM);
  CefRefPtr<CefV8Value> result;
  CefRefPtr<CefV8Exception> exception;
  context->Eval(kPageScript, frame->GetURL(), 0, result, exception);
  // Cosmetic filtering for web pages (007); the browser decides whether
  // anything applies. Before the page's scripts, in every frame.
  std::string frame_url = frame->GetURL().ToString();
  if (frame_url.starts_with("http://") || frame_url.starts_with("https://")) {
    global->SetValue(
        kAdblockFunction,
        CefV8Value::CreateFunction(kAdblockFunction, new AdblockHandler(this)),
        V8_PROPERTY_ATTRIBUTE_DONTENUM);
    context->Eval(kAdblockCosmeticScript, "brouk://adblock-cosmetic.js", 0,
                  result, exception);
  }
  // Login forms (008), in every http(s) frame; the browser decides what to
  // fill and where.
  if (frame_url.starts_with("http://") || frame_url.starts_with("https://")) {
    global->SetValue(
        kFormsFunction,
        CefV8Value::CreateFunction(kFormsFunction, new FormsHandler(this)),
        V8_PROPERTY_ATTRIBUTE_DONTENUM);
    context->Eval(std::string("(function () {\n") + kFormsScript + "\n})();",
                  "brouk://forms.js", 0, result, exception);
  }
  // The JSON viewer checks the document itself once it has loaded. Wrapped
  // in a function, so none of its names reach the page's global scope.
  if (json_viewer_ && frame->IsMain()) {
    context->Eval(
        std::string("(function () {\n") + kJsonViewerScript + "\n})();",
        "brouk://json-viewer.js", 0, result, exception);
  }
}

void RendererApp::OnWebKitInitialized() {
  CEF_REQUIRE_RENDERER_THREAD();
  // The settings at the time this process started (see BrowserApp).
  CefRefPtr<CefCommandLine> command_line =
      CefCommandLine::GetGlobalCommandLine();
  const std::string json_switch(kRendererJsonViewerSwitch);
  if (command_line && command_line->HasSwitch(json_switch)) {
    json_viewer_ = command_line->GetSwitchValue(json_switch) != "0";
  }
}

void RendererApp::ApplyConfig(const CefRefPtr<CefDictionaryValue>& config) {
  const std::string key(kRendererConfigJsonViewer);
  if (config && config->GetType(key) == VTYPE_BOOL) {
    json_viewer_ = config->GetBool(key);
  }
}

void RendererApp::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    CefRefPtr<CefV8Context> context) {
  CEF_REQUIRE_RENDERER_THREAD();
  router_->OnContextReleased(browser, frame, context);
  auto receiver = adblock_receivers_.find(frame->GetIdentifier().ToString());
  if (receiver != adblock_receivers_.end() &&
      receiver->second.context->IsSame(context)) {
    adblock_receivers_.erase(receiver);
  }
  auto forms = forms_receivers_.find(frame->GetIdentifier().ToString());
  if (forms != forms_receivers_.end() &&
      forms->second.context->IsSame(context)) {
    forms_receivers_.erase(forms);
  }
  auto it = display_media_handlers_.find(frame->GetIdentifier().ToString());
  if (it != display_media_handlers_.end() &&
      it->second.context->IsSame(context)) {
    display_media_handlers_.erase(it);
  }
}

void RendererApp::SetFormsReceiver(CefRefPtr<CefV8Context> context,
                                   CefRefPtr<CefV8Value> function) {
  CEF_REQUIRE_RENDERER_THREAD();
  CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
  if (frame) {
    // Only the script's own call counts: it runs before the page's scripts,
    // which cannot reach the native function afterwards.
    forms_receivers_.try_emplace(frame->GetIdentifier().ToString(),
                                 DisplayMediaHandler{context, function});
  }
}

void RendererApp::SetAdblockReceiver(CefRefPtr<CefV8Context> context,
                                     CefRefPtr<CefV8Value> function) {
  CEF_REQUIRE_RENDERER_THREAD();
  CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
  if (frame) {
    adblock_receivers_[frame->GetIdentifier().ToString()] = {context, function};
  }
}

// Runs the page's scriptlets in its own world (as uBlock Origin injects
// them) and hands the rest to the cosmetic filtering script.
void RendererApp::DeliverAdblockAnswer(const CefRefPtr<CefFrame>& frame,
                                       const std::string& json,
                                       const std::string& script) {
  auto it = adblock_receivers_.find(frame->GetIdentifier().ToString());
  if (it == adblock_receivers_.end() || !it->second.context->Enter()) {
    return;
  }
  if (!script.empty()) {
    CefRefPtr<CefV8Value> result;
    CefRefPtr<CefV8Exception> exception;
    it->second.context->Eval(script, "brouk://adblock-scriptlets.js", 0, result,
                             exception);
  }
  CefV8ValueList arguments = {CefV8Value::CreateString(json)};
  it->second.function->ExecuteFunction(nullptr, arguments);
  it->second.context->Exit();
}

void RendererApp::SetDisplayMediaHandler(CefRefPtr<CefV8Context> context,
                                         CefRefPtr<CefV8Value> function) {
  CEF_REQUIRE_RENDERER_THREAD();
  CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
  if (!frame) {
    return;
  }
  // Only the page script's first call counts (it runs before the page's own
  // scripts, which cannot reach the native function afterwards).
  display_media_handlers_.try_emplace(frame->GetIdentifier().ToString(),
                                      DisplayMediaHandler{context, function});
}

bool RendererApp::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (router_->OnProcessMessageReceived(browser, frame, source_process,
                                        message)) {
    return true;
  }
  if (source_process == PID_BROWSER &&
      message->GetName().ToString() == kRendererConfigMessage) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() == 1 && args->GetType(0) == VTYPE_DICTIONARY) {
      ApplyConfig(args->GetDictionary(0));
    }
    return true;
  }
  std::string name = message->GetName().ToString();
  if (source_process == PID_BROWSER && frame && name == kAdblockPick) {
    // The callback and the script that takes it run in one go: the page
    // gets no chance to call it.
    CefRefPtr<CefV8Context> context = frame->GetV8Context();
    if (context && context->Enter()) {
      context->GetGlobal()->SetValue(
          "__broukPicked",
          CefV8Value::CreateFunction("__broukPicked", new PickedHandler()),
          V8_PROPERTY_ATTRIBUTE_DONTENUM);
      CefRefPtr<CefV8Value> result;
      CefRefPtr<CefV8Exception> exception;
      context->Eval(kAdblockPickerScript, "brouk://adblock-picker.js", 0,
                    result, exception);
      context->Exit();
    }
    return true;
  }
  if (source_process == PID_BROWSER && frame && name == kFormsFillMessage) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    auto it = forms_receivers_.find(frame->GetIdentifier().ToString());
    if (args && args->GetSize() == 1 && args->GetType(0) == VTYPE_STRING &&
        it != forms_receivers_.end() && it->second.context->Enter()) {
      CefV8ValueList arguments = {CefV8Value::CreateString(args->GetString(0))};
      it->second.function->ExecuteFunction(nullptr, arguments);
      it->second.context->Exit();
    }
    return true;
  }
  if (source_process == PID_BROWSER && frame &&
      (name == kAdblockCosmeticAnswer || name == kAdblockClassesAnswer)) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() >= 1 && args->GetType(0) == VTYPE_STRING) {
      std::string script =
          args->GetSize() >= 2 && args->GetType(1) == VTYPE_STRING
              ? args->GetString(1).ToString()
              : "";
      DeliverAdblockAnswer(frame, args->GetString(0).ToString(), script);
    }
    return true;
  }
  if (source_process != PID_BROWSER || name != kDisplayMediaChoiceMessage) {
    return false;
  }
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (!frame || !args || args->GetSize() != 2 ||
      args->GetType(0) != VTYPE_DOUBLE || args->GetType(1) != VTYPE_STRING) {
    return true;
  }
  double id = args->GetDouble(0);
  std::string source = args->GetString(1).ToString();
  if (!(id >= 0 && id <= UINT32_MAX && std::floor(id) == id) ||
      (!source.empty() && !IsValidDesktopSourceId(source))) {
    return true;
  }
  auto it = display_media_handlers_.find(frame->GetIdentifier().ToString());
  if (it == display_media_handlers_.end() || !it->second.context->Enter()) {
    return true;
  }
  CefV8ValueList arguments = {
      CefV8Value::CreateUInt(static_cast<std::uint32_t>(id)),
      CefV8Value::CreateString(source)};
  it->second.function->ExecuteFunction(nullptr, arguments);
  it->second.context->Exit();
  return true;
}

}  // namespace brouk
