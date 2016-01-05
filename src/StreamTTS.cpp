/*******************************************************************************
* Copyright (c) 2015,2016 IBM Corp.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/
#include <sndfile.h>
#include "StreamTTS.hpp"

StreamTTS::StreamTTS(const char* username, const char* passwd, Engine engine, v8::Local<v8::Function> func)
        : mUserName(strdup(username)), mPasswd(strdup(passwd)),
          mEngine(engine) {

    mFunc.Reset(func->CreationContext()->GetIsolate(), func);
    SF_INFO sfinfo;
    SNDFILE* silent = sf_open("./voice/interlude/empty.wav", SFM_READ, &sfinfo);
    if (!silent) {
        printf("can't open silent wav: %s\n", sf_strerror(NULL));
        return;
    }
    sf_count_t count = sf_readf_short(silent, (short*) mSilent, ONE_SEC_FRAMES) * 2;
    if ( engine == kWatson ) {
        char p = '0';
        for (sf_count_t i = 0; i < count ; i+=2) {
            p = mSilent[i];
            mSilent[i] = mSilent[i+1];
            mSilent[i+1] = p;
        }
    }
    sf_close(silent);
}

StreamTTS::~StreamTTS() {
    free(mUserName);
    free(mPasswd);
    mObj.Reset();
    mFunc.Reset();
    mCB.Reset();
}

/*static*/
void
StreamTTS::sListening(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Local<v8::Context> ctx = info.GetIsolate()->GetCurrentContext();
    v8::Local<v8::Object> data = info.Data()->ToObject(ctx).ToLocalChecked();
    assert(data->InternalFieldCount() > 0);
    StreamTTS* _this = reinterpret_cast<StreamTTS*>(data->GetAlignedPointerFromInternalField(0));
    _this->mConn.Reset(info.GetIsolate(), info[0]->ToObject(ctx).ToLocalChecked());
}

v8::Local<v8::Function>
StreamTTS::GetListeningFunction(v8::Isolate* isolate) {
    v8::EscapableHandleScope scope(isolate);
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    v8::Local<v8::ObjectTemplate> otmpl = v8::ObjectTemplate::New(isolate);
    otmpl->SetInternalFieldCount(1);
    v8::Local<v8::Object> obj = otmpl->NewInstance(ctx).ToLocalChecked();
    obj->SetAlignedPointerInInternalField(0, this);
    return scope.Escape(v8::Function::New(ctx, sListening, obj, 1).ToLocalChecked());
}

void
StreamTTS::CreateSession() {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, mFunc);
    v8::Local<v8::Function> cb = v8::Local<v8::Function>::New(isolate, mCB);
    v8::Local<v8::String> username = v8::String::NewFromUtf8(isolate, mUserName);
    v8::Local<v8::String> passwd = v8::String::NewFromUtf8(isolate, mPasswd);
    v8::Local<v8::Value> args[] = { username, passwd, cb };
    v8::Local<v8::Value> rev;
    if ( func->Call(ctx, ctx->Global(),3, args).ToLocal(&rev) ) {
        v8::Local<v8::Object> session = rev->ToObject(ctx).ToLocalChecked();
        mObj.Reset(isolate, session);
        if ( mEngine == kWatson ) {
            //watson need the connection object to perform the close();
            v8::Local<v8::Value> cb[] = { Nan::New("connect").ToLocalChecked(), GetListeningFunction(isolate) };
            Nan::MakeCallback(session, "on", 2, cb);
        }
    }
}

/*static*/
void StreamTTS::sFreeHandle(uv_handle_t* handle) {
    delete handle;
}

/*static*/
void StreamTTS::sCreateSession(uv_async_t* handle) {
    StreamTTS* _this = (StreamTTS*)handle->data;
    _this->CreateSession();
    uv_close(reinterpret_cast<uv_handle_t*>(handle), sFreeHandle);
}

/*static*/
void StreamTTS::sWrite(uv_async_t* handle) {
    WriteData* wd = (WriteData*)handle->data;
    wd->mThis->Write(wd);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), sFreeHandle);
}

void
StreamTTS::WSConnect() {
    uv_async_t* async = new uv_async_t;
    async->data = this;
    uv_async_init(uv_default_loop(), async,
            sCreateSession);
    uv_async_send(async);
}

void
StreamTTS::Stop() {
    uv_async_t* async = new uv_async_t;
    async->data = this;
    uv_async_init(uv_default_loop(), async,
            sCloseSession);
    uv_async_send(async);
}

/*static*/
void StreamTTS::sCloseSession(uv_async_t* handle) {
    StreamTTS* _this = (StreamTTS*)handle->data;
    _this->CloseSession();
    uv_close(reinterpret_cast<uv_handle_t*>(handle), sFreeHandle);
}

void
StreamTTS::CloseSession() {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);
    if ( mEngine == kWatson ) {
        if ( mConn.IsEmpty() ) {
            return;
        }
        v8::Local<v8::Object> conn = v8::Local<v8::Object>::New(isolate, mConn);
        Nan::MakeCallback(conn, "close", 0, NULL);
    } else if ( mEngine == kGoogle ) {
        v8::Local<v8::Object> req = v8::Local<v8::Object>::New(isolate, mObj);
        Nan::MakeCallback(req, "end", 0, NULL);
    }
    mObj.Reset();
}

void
StreamTTS::Write(WriteData* data) {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);
    if ( mObj.IsEmpty() ) {
        return;
    }
    v8::Local<v8::Object> obj = v8::Local<v8::Object>::New(isolate, mObj);
    v8::Local<v8::Object> buff;
    if ( Nan::NewBuffer(data->mData, data->mSize, sFreeCallback, data).ToLocal(&buff) ) {
        v8::Local<v8::Value> args[] = { buff };
        Nan::MakeCallback(obj, "write", 1, args);
    }
}

/*static*/
void StreamTTS::sFreeCallback(char* data, void* hint) {
    WriteData* wd = (WriteData*)hint;
    delete wd;
}

void
StreamTTS::SendVoiceWav(char* data, uint32_t length) {
    if ( length > 0 ) {
        uv_async_t* async = new uv_async_t;
        async->data = new WriteData(data, length, this, true, mEngine == kWatson);
        uv_async_init(uv_default_loop(), async,
                sWrite);
        uv_async_send(async);
    }
}

void
StreamTTS::SendSilentWav() {
    uv_async_t* async = new uv_async_t;
    async->data = new WriteData(mSilent, ONE_SEC_FRAMES*2, this, false);
    uv_async_init(uv_default_loop(), async,
            sWrite);
    uv_async_send(async);
}

void
StreamTTS::SendIdleSilent() {
    SendSilentWav();
}
