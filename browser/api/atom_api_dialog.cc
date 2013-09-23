// Copyright (c) 2013 GitHub, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "browser/api/atom_api_dialog.h"

#include <string>

#include "base/bind.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "browser/api/atom_api_window.h"
#include "browser/native_window.h"
#include "browser/ui/file_dialog.h"
#include "browser/ui/message_box.h"
#include "vendor/node/src/node_internals.h"

using node::node_isolate;

namespace atom {

namespace api {

namespace {

// Trick to overload functions by return value's type.
struct FromV8Value {
  explicit FromV8Value(v8::Handle<v8::Value> value) : value_(value) {}

  operator std::string() {
    return *v8::String::Utf8Value(value_);
  }

  operator int() {
    return value_->IntegerValue();
  }

  operator base::FilePath() {
    return base::FilePath::FromUTF8Unsafe(FromV8Value(value_));
  }

  operator atom::NativeWindow*() {
    if (value_->IsObject()) {
      Window* window = Window::Unwrap<Window>(value_->ToObject());
      if (window && window->window())
        return window->window();
    }
    return NULL;
  }

  operator v8::Persistent<v8::Function>() {
    return value_->IsFunction() ?
      v8::Persistent<v8::Function>::New(
          node_isolate,
          v8::Handle<v8::Function>::Cast(value_)) :
      v8::Persistent<v8::Function>();
  }

  v8::Handle<v8::Value> value_;
};

v8::Handle<v8::Value> ToV8Value(const base::FilePath& path) {
  std::string path_string(path.AsUTF8Unsafe());
  return v8::String::New(path_string.data(), path_string.size());
}

v8::Handle<v8::Value> ToV8Value(void* whatever) {
  return v8::Undefined();
}

v8::Handle<v8::Value> ToV8Value(int code) {
  return v8::Integer::New(code);
}

v8::Handle<v8::Value> ToV8Value(const std::vector<base::FilePath>& paths) {
  v8::Handle<v8::Array> result = v8::Array::New(paths.size());
  for (size_t i = 0; i < paths.size(); ++i)
    result->Set(i, ToV8Value(paths[i]));
  return result;
}

template<typename T>
void CallV8Function(v8::Persistent<v8::Function> callback, T arg) {
  DCHECK(!callback.IsEmpty());

  v8::HandleScope scope;
  v8::Handle<v8::Value> value = ToV8Value(arg);
  callback->Call(callback, 1, &value);
  callback.Dispose(node_isolate);
}

template<typename T>
void CallV8Function2(v8::Persistent<v8::Function> callback,
                     bool result,
                     T arg) {
  if (result)
    return CallV8Function<T>(callback, arg);
  else
    return CallV8Function<void*>(callback, NULL);
}

void Initialize(v8::Handle<v8::Object> target) {
  v8::HandleScope scope;

  NODE_SET_METHOD(target, "showMessageBox", ShowMessageBox);
  NODE_SET_METHOD(target, "showOpenDialog", ShowOpenDialog);
  NODE_SET_METHOD(target, "showSaveDialog", ShowSaveDialog);
}

}  // namespace

v8::Handle<v8::Value> ShowMessageBox(const v8::Arguments &args) {
  v8::HandleScope scope;

  if (!args[0]->IsNumber() ||  // type
      !args[1]->IsArray() ||   // buttons
      !args[2]->IsString() ||  // title
      !args[3]->IsString() ||  // message
      !args[4]->IsString())    // detail
    return node::ThrowTypeError("Bad argument");

  NativeWindow* native_window = FromV8Value(args[5]);
  v8::Persistent<v8::Function> callback = FromV8Value(args[6]);

  MessageBoxType type = (MessageBoxType)(args[0]->IntegerValue());

  std::vector<std::string> buttons;
  v8::Handle<v8::Array> v8_buttons = v8::Handle<v8::Array>::Cast(args[1]);
  for (uint32_t i = 0; i < v8_buttons->Length(); ++i)
    buttons.push_back(FromV8Value(v8_buttons->Get(i)));

  std::string title = FromV8Value(args[2]);
  std::string message = FromV8Value(args[3]);
  std::string detail = FromV8Value(args[4]);

  if (callback.IsEmpty()) {
    int chosen = atom::ShowMessageBox(
        native_window, type, buttons, title, message, detail);
    return scope.Close(v8::Integer::New(chosen));
  } else {
    atom::ShowMessageBox(
        native_window,
        type,
        buttons,
        title,
        message,
        detail,
        base::Bind(&CallV8Function<int>, callback));
    return v8::Undefined();
  }
}

v8::Handle<v8::Value> ShowOpenDialog(const v8::Arguments &args) {
  v8::HandleScope scope;

  if (!args[0]->IsString() ||  // title
      !args[1]->IsString() ||  // default_path
      !args[2]->IsNumber())    // properties
    return node::ThrowTypeError("Bad argument");

  NativeWindow* native_window = FromV8Value(args[3]);
  v8::Persistent<v8::Function> callback = FromV8Value(args[4]);

  std::string title = FromV8Value(args[0]);
  base::FilePath default_path = FromV8Value(args[1]);
  int properties = FromV8Value(args[2]);

  if (callback.IsEmpty()) {
    std::vector<base::FilePath> paths;
    if (!file_dialog::ShowOpenDialog(native_window,
                                     title,
                                     default_path,
                                     properties,
                                     &paths))
      return v8::Undefined();

    v8::Handle<v8::Array> result = v8::Array::New(paths.size());
    for (size_t i = 0; i < paths.size(); ++i)
      result->Set(i, ToV8Value(paths[i]));

    return scope.Close(result);
  } else {
    file_dialog::ShowOpenDialog(
        native_window,
        title,
        default_path,
        properties,
        base::Bind(&CallV8Function2<const std::vector<base::FilePath>&>,
                   callback));
    return v8::Undefined();
  }
}

v8::Handle<v8::Value> ShowSaveDialog(const v8::Arguments &args) {
  v8::HandleScope scope;

  if (!args[0]->IsString() ||  // title
      !args[1]->IsString())    // default_path
    return node::ThrowTypeError("Bad argument");

  NativeWindow* native_window = FromV8Value(args[2]);
  v8::Persistent<v8::Function> callback = FromV8Value(args[3]);

  std::string title = FromV8Value(args[0]);
  base::FilePath default_path = FromV8Value(args[1]);

  if (callback.IsEmpty()) {
    base::FilePath path;
    if (!file_dialog::ShowSaveDialog(native_window,
                                     title,
                                     default_path,
                                     &path))
      return v8::Undefined();

    return scope.Close(ToV8Value(path));
  } else {
    file_dialog::ShowSaveDialog(
        native_window,
        title,
        default_path,
        base::Bind(&CallV8Function2<const base::FilePath&>, callback));
    return v8::Undefined();
  }
}

}  // namespace api

}  // namespace atom

NODE_MODULE(atom_browser_dialog, atom::api::Initialize)
