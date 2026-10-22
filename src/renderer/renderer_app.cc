#include "renderer/renderer_app.h"

#include <cmath>
#include <cstdint>
#include <string>

#include "core/page_events.h"
#include "core/screen_share.h"
#include "include/cef_process_message.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Names of the native functions while the page script starts; the script
// removes them from the page's global object right away.
constexpr char kReportFunction[] = "__broukReport";
constexpr char kSetDisplayMediaHandlerFunction[] =
    "__broukSetDisplayMediaHandler";

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

}  // namespace

void RendererApp::OnContextCreated(CefRefPtr<CefBrowser> /*browser*/,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefV8Context> context) {
  CEF_REQUIRE_RENDERER_THREAD();
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
}

void RendererApp::OnContextReleased(CefRefPtr<CefBrowser> /*browser*/,
                                    CefRefPtr<CefFrame> frame,
                                    CefRefPtr<CefV8Context> context) {
  CEF_REQUIRE_RENDERER_THREAD();
  auto it = display_media_handlers_.find(frame->GetIdentifier().ToString());
  if (it != display_media_handlers_.end() &&
      it->second.context->IsSame(context)) {
    display_media_handlers_.erase(it);
  }
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
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (source_process != PID_BROWSER ||
      message->GetName().ToString() != kDisplayMediaChoiceMessage) {
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
