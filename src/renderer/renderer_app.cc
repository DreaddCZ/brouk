#include "renderer/renderer_app.h"

#include <string>

#include "core/page_events.h"
#include "include/cef_process_message.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Name of the native reporting function while the page script starts; the
// script removes it from the page's global object right away.
constexpr char kReportFunction[] = "__broukReport";

// Reports blocked popups (window.open returns null when Chromium's popup
// blocker stopped it) and whether the page plays audio: a media element
// playing with sound, or a running Web Audio context. Approximates Chrome's
// tab audio indicator, which CEF does not expose.
constexpr char kPageScript[] = R"js(
(function () {
  'use strict';
  const report = window.__broukReport;
  try { delete window.__broukReport; } catch (e) {}
  if (typeof report !== 'function') return;

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
  CefRefPtr<CefV8Value> result;
  CefRefPtr<CefV8Exception> exception;
  context->Eval(kPageScript, frame->GetURL(), 0, result, exception);
}

}  // namespace brouk
