#include "capture.h"

#include <nan.h>
#include <node.h>
#include <v8.h>
#include <uv.h>

#include <errno.h>
#include <string.h>

#include <string>
#include <sstream>

using v8::Function;
using v8::FunctionTemplate;
using v8::ObjectTemplate;
using v8::Handle;
using v8::Object;
using v8::String;
using v8::Number;
using v8::Boolean;
using v8::Local;
using v8::Value;
using v8::Array;
using v8::Persistent;

struct CallbackData {
  Persistent<Object> thisObj;
  Nan::Callback* callback;
};

class Camera : public Nan::ObjectWrap {
  public:
    static void Init(Handle<Object> exports);
  private:
    static NAN_METHOD(New);
    static NAN_METHOD(Start);
    static NAN_METHOD(Stop);
    static NAN_METHOD(Capture);
    static NAN_METHOD(ToYUYV);
    static NAN_METHOD(ToRGB);
    static NAN_METHOD(ConfigGet);
    static NAN_METHOD(ConfigSet);
    static NAN_METHOD(ControlGet);
    static NAN_METHOD(ControlSet);

    static Local<Object> Controls(camera_t* camera);
    static Local<Object> Formats(camera_t* camera);
    static void StopCB(uv_poll_t* handle, int status, int events);
    static void CaptureCB(uv_poll_t* handle, int status, int events);

    static void
    WatchCB(uv_poll_t* handle, void (*callbackCall)(CallbackData* data));
    static void Watch(Handle<Object> thisObj, Nan::Callback* cb1, uv_poll_cb cb);
    Camera();
    ~Camera();
    camera_t* camera;
};

//[error message handling]
struct LogContext {
  std::string msg;
};
static void logRecord(camera_log_t type, const char* msg, void* pointer) {
  std::stringstream ss;
  switch (type) {
  case CAMERA_ERROR:
    ss << "CAMERA ERROR [" << msg << "] " << errno << " " << strerror(errno);
    break;
  case CAMERA_FAIL:
    ss << "CAMERA FAIL [" << msg << "]";
    break;
  case CAMERA_INFO:
    ss << "CAMERA INFO [" << msg << "]";
    break;
  }
  static_cast<LogContext*>(pointer)->msg = ss.str();
}

//[helpers]
static inline Local<Value>
getValue(const Local<Object>& self, const char* name) {
  return  Nan::Get(self, Nan::New<String>(name).ToLocalChecked()).ToLocalChecked();
}
static inline int32_t
getInt(const Local<Object>& self, const char* name) {
  return getValue(self, name)->Int32Value();
}
static inline uint32_t
getUint(const Local<Object>& self, const char* name) {
  return getValue(self, name)->Uint32Value();
}

static inline void
setValue(const Local<Object>& self, const char* name,
         const Handle<Value>& value) {
  Nan::Set(self, Nan::New<String>(name).ToLocalChecked(), value);
}
static inline void
setInt(const Local<Object>& self, const char* name, int32_t value) {
  setValue(self, name, Nan::New<Number>(value));
}
static inline void
setUint(const Local<Object>& self, const char* name,
        uint32_t value) {
  setValue(self, name, Nan::New<Number>(value));
}
static inline void
setString(const Local<Object>& self, const char* name,
          const char* value) {
  setValue(self, name, Nan::New<String>(value).ToLocalChecked());
}
static inline void
setBool(const Local<Object>& self, const char* name, bool value) {
  setValue(self, name, Nan::New<Boolean>(value));
}

//[callback helpers]
void Camera::WatchCB(uv_poll_t* handle, void (*callbackCall)(CallbackData* data)) {
  Nan::HandleScope scope;
  auto data = static_cast<CallbackData*>(handle->data);
  uv_poll_stop(handle);
  uv_close(reinterpret_cast<uv_handle_t*>(handle),
           [](uv_handle_t* handle) -> void {delete handle;});
  callbackCall(data);
  data->thisObj.Reset();
  delete data->callback;
  delete data;
}

void Camera::Watch(Local<Object> thisObj, Nan::Callback* cb1, uv_poll_cb cb) {
  auto data = new CallbackData;
  data->thisObj.Reset(v8::Isolate::GetCurrent(), thisObj);
  data->callback = cb1;
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;

  uv_poll_t* handle = new uv_poll_t;
  handle->data = data;
  uv_poll_init(uv_default_loop(), handle, camera->fd);
  uv_poll_start(handle, UV_READABLE, cb);
}

//[methods]

static const char* control_type_names[] = {
  "invalid",
  "int",
  "bool",
  "menu",
  "int64",
  "class"
  "string",
  "bitmask",
  "int_menu",
};

Local<Object> Camera::Controls (camera_t* camera) {
  auto ccontrols = camera_controls_new(camera);
  auto controls = Nan::New<Array>(ccontrols->length);
  for (size_t i = 0; i < ccontrols->length; i++) {
    auto ccontrol = &ccontrols->head[i];
    auto control = Nan::New<Object>();
    auto name = Nan::New<String>(reinterpret_cast<char*>(ccontrol->name)).ToLocalChecked();
    Nan::Set(controls, i, control);
    Nan::Set(controls, name, control);
    setUint(control, "id", ccontrol->id);
    setValue(control, "name", name);
    setString(control, "type", control_type_names[ccontrol->type]);
    setInt(control, "min", ccontrol->min);
    setInt(control, "max", ccontrol->max);
    setInt(control, "step", ccontrol->step);
    setInt(control, "default", ccontrol->default_value);

    auto flags = Nan::New<Object>();
    setValue(control, "flags", flags);
    setBool(flags, "disabled", ccontrol->flags.disabled);
    setBool(flags, "grabbed", ccontrol->flags.grabbed);
    setBool(flags, "readOnly", ccontrol->flags.read_only);
    setBool(flags, "update", ccontrol->flags.update);
    setBool(flags, "inactive", ccontrol->flags.inactive);
    setBool(flags, "slider", ccontrol->flags.slider);
    setBool(flags, "writeOnly", ccontrol->flags.write_only);
    setBool(flags, "volatile", ccontrol->flags.volatile_value);

    auto menu = Nan::New<Array>(ccontrol->menus.length);
    setValue(control, "menu", menu);
    switch (ccontrol->type) {
    case CAMERA_CTRL_MENU:
      for (size_t j = 0; j < ccontrol->menus.length; j++) {
        auto value = reinterpret_cast<char*>(ccontrol->menus.head[j].name);
        menu->Set(j, Nan::New<String>(value).ToLocalChecked());
      }
      break;
#ifndef CAMERA_OLD_VIDEODEV2_H
    case CAMERA_CTRL_INTEGER_MENU:
      for (size_t j = 0; j < ccontrol->menus.length; j++) {
        auto value = static_cast<int32_t>(ccontrol->menus.head[j].value);
        menu->Set(j, Nan::New<Number>(value));
      }
      break;
#endif
    default: break;
    }
  }
  camera_controls_delete(ccontrols);
  return controls;
}

Local<Object> convertFormat(camera_format_t* cformat) {
  char name[5];
  camera_format_name(cformat->format, name);
  auto format = Nan::New<Object>();
  setString(format, "formatName", name);
  setUint(format, "format", cformat->format);
  setUint(format, "width", cformat->width);
  setUint(format, "height", cformat->height);
  auto interval = Nan::New<Object>();
  setValue(format, "interval", interval);
  setUint(interval, "numerator", cformat->interval.numerator);
  setUint(interval, "denominator", cformat->interval.denominator);
  return format;
}

Local<Object> Camera::Formats(camera_t* camera) {
  auto cformats = camera_formats_new(camera);
  auto formats = Nan::New<Array>(cformats->length);
  for (size_t i = 0; i < cformats->length; i++) {
    auto cformat = &cformats->head[i];
    auto format = convertFormat(cformat);
    formats->Set(i, format);
  }
  return formats;
}

NAN_METHOD(Camera::New) {
  Nan::HandleScope scope;
  if (info.Length() < 1) {
    Nan::ThrowTypeError("argument required: device");
  }
  String::Utf8Value device(info[0]->ToString());
  auto camera = camera_open(*device);
  if (!camera) {
    Nan::ThrowError(strerror(errno));
  }
  camera->context.pointer = new LogContext;
  camera->context.log = &logRecord;

  auto thisObj = info.This();
  auto self = new Camera();
  self->camera = camera;
  self->Wrap(thisObj);
  setValue(thisObj, "device", info[0]);
  setValue(thisObj, "formats", Formats(camera));
  setValue(thisObj, "controls", Controls(camera));
  info.GetReturnValue().Set(thisObj);
}

NAN_METHOD(Camera::Start) {
  auto thisObj = info.This();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  if (!camera_start(camera)) {
    Nan::ThrowError("Camera cannot start");
  }
  setUint(thisObj, "width", camera->width);
  setUint(thisObj, "height", camera->height);
  info.GetReturnValue().Set(thisObj);
}


void Camera::StopCB(uv_poll_t* handle, int /*status*/, int /*events*/) {
  auto callCallback = [](CallbackData* data) -> void {
    Nan::HandleScope scope;
    Local<Object> thisObj = Nan::New(data->thisObj);
    data->callback->Call(thisObj, 0, nullptr);
  };
  WatchCB(handle, callCallback);
}

NAN_METHOD(Camera::Stop) {
  Nan::HandleScope scope;
  auto thisObj = info.This();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  if (!camera_stop(camera)) {
    Nan::ThrowError("Camera cannot stop");
  }
  Watch(info.This(), new Nan::Callback(info[0].As<Function>()), StopCB);
  info.GetReturnValue().Set(Nan::Undefined());
}

void Camera::CaptureCB(uv_poll_t* handle, int /*status*/, int /*events*/) {
  auto callCallback = [](CallbackData* data) -> void {
    Nan::HandleScope scope;
    Local<Object> thisObj = Nan::New(data->thisObj);
    auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
    bool captured = camera_capture(camera);
    Local<Value> argv[] = {
      Nan::New<Boolean>(captured),
    };
    data->callback->Call(thisObj, 1, argv);
  };
  WatchCB(handle, callCallback);
}

NAN_METHOD(Camera::Capture) {
  Watch(info.This(), new Nan::Callback(info[0].As<Function>()), CaptureCB);
  info.GetReturnValue().Set(Nan::Undefined());
}

NAN_METHOD(Camera::ToYUYV) {
  Nan::HandleScope scope;
  auto thisObj = info.This();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  int size = camera->width * camera->height * 2;
  Local<Object> ret = Nan::CopyBuffer((const char*) camera->head.start, size).ToLocalChecked();
  info.GetReturnValue().Set(ret);
}

NAN_METHOD(Camera::ToRGB) {
  Nan::HandleScope scope;
  auto thisObj = info.This();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  auto rgb = yuyv2rgb(camera->head.start, camera->width, camera->height);
  int size = camera->width * camera->height * 3;
  Local<Object> ret = Nan::NewBuffer((char*) rgb, size).ToLocalChecked();
  info.GetReturnValue().Set(ret);
}

NAN_METHOD(Camera::ConfigGet) {
  Nan::HandleScope scope;
  auto thisObj = info.This();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  camera_format_t cformat;
  if (!camera_config_get(camera, &cformat)) {
    Nan::ThrowError("Cannot get configuration");
  }
  auto format = convertFormat(&cformat);
  info.GetReturnValue().Set(format);
}

NAN_METHOD(Camera::ConfigSet) {
  Nan::HandleScope scope;
  if (info.Length() < 1) {
    Nan::ThrowTypeError("argument required: config");
  }
  auto format = info[0]->ToObject();
  uint32_t width = getUint(format, "width");
  uint32_t height = getUint(format, "height");
  uint32_t numerator = 0;
  uint32_t denominator = 0;
  auto finterval = getValue(format, "interval");
  if (finterval->IsObject()) {
    auto interval = finterval->ToObject();
    numerator = getUint(interval, "numerator");
    denominator = getUint(interval, "denominator");
  }
  camera_format_t cformat = {0, width, height, {numerator, denominator}};
  auto thisObj = info.This();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  if (!camera_config_set(camera, &cformat)) {
    Nan::ThrowError("Cannot set configuration");
  }
  setUint(thisObj, "width", camera->width);
  setUint(thisObj, "height", camera->height);
  info.GetReturnValue().Set(thisObj);
}

NAN_METHOD(Camera::ControlGet) {
  Nan::HandleScope scope;
  if (info.Length() < 1) {
    return Nan::ThrowTypeError("an argument required: id");
  }
  uint32_t id = info[0]->Uint32Value();
  auto thisObj = info.This();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  int32_t value = 0;
  bool success = camera_control_get(camera, id, &value);
  if (!success) {
    Nan::ThrowError("Cannot get camera control.");
  }
  info.GetReturnValue().Set(Nan::New<Number>(value));
}

NAN_METHOD(Camera::ControlSet) {
  Nan::HandleScope scope;
  if (info.Length() < 2) {
    Nan::ThrowTypeError("arguments required: id, value");
  }
  uint32_t id = info[0]->Uint32Value();
  int32_t value = info[1]->Int32Value();
  auto thisObj = info.This();
  auto camera = Nan::ObjectWrap::Unwrap<Camera>(thisObj)->camera;
  bool success = camera_control_set(camera, id, value);
  if (!success) {
    Nan::ThrowError("Cannot set camera control.");
  }
  info.GetReturnValue().Set(thisObj);
}

Camera::Camera() : camera(nullptr) {}
Camera::~Camera() {
  if (camera) {
    auto ctx = static_cast<LogContext*>(camera->context.pointer);
    camera_close(camera);
    delete ctx;
  }
}


//[module init]
static inline void
setMethod(const Local<ObjectTemplate>& proto, const char* name, NAN_METHOD(func)) {
  auto funcValue = Nan::New<FunctionTemplate>(func)->GetFunction();
  proto->Set(Nan::New<String>(name).ToLocalChecked(), funcValue);
}

void Camera::Init(Handle<Object> exports) {
  auto name = Nan::New<String>("Camera");
  auto clazz = Nan::New<FunctionTemplate>(New);
  clazz->SetClassName(name.ToLocalChecked());
  clazz->InstanceTemplate()->SetInternalFieldCount(1);

  auto proto = clazz->PrototypeTemplate();
  setMethod(proto, "start", Start);
  setMethod(proto, "stop", Stop);
  setMethod(proto, "capture", Capture);
  setMethod(proto, "toYUYV", ToYUYV);
  setMethod(proto, "toRGB", ToRGB);
  setMethod(proto, "configGet", ConfigGet);
  setMethod(proto, "configSet", ConfigSet);
  setMethod(proto, "controlGet", ControlGet);
  setMethod(proto, "controlSet", ControlSet);
  Local<Function> ctor = clazz->GetFunction();
  Nan::Set(exports, name.ToLocalChecked(), ctor);
}

NODE_MODULE(v4l2camera, Camera::Init)
