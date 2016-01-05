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
#include "watsontts.hpp"

#include <sndfile.h>

WatsonTTS::WatsonTTS(const char* username, const char* passwd, v8::Local<v8::Function> func)
    : mUserName(strdup(username)), mPasswd(strdup(passwd)){
    mFunc.Reset(func->CreationContext()->GetIsolate(), func);
    SF_INFO sfinfo;
    SNDFILE* silent = sf_open("./voice/interlude/empty.wav", SFM_READ, &sfinfo);
    if (!silent) {
        printf("can't open silent wav: %s\n", sf_strerror(NULL));
        return;
    }
    sf_count_t count = sf_readf_short(silent, (short*) mSilent, ONE_SEC_FRAMES) * 2;
    char p = '0';
    for (sf_count_t i = 0; i < count ; i+=2) {
        p = mSilent[i];
        mSilent[i] = mSilent[i+1];
        mSilent[i+1] = p;
    }
    sf_close(silent);
}

WatsonTTS::~WatsonTTS() {
    free(mUserName);
    free(mPasswd);
    mObj.Reset();
    mFunc.Reset();
    mCB.Reset();
}

/*static*/
void
WatsonTTS::sListening(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Local<v8::Context> ctx = info.GetIsolate()->GetCurrentContext();
    v8::Local<v8::Object> data = info.Data()->ToObject(ctx).ToLocalChecked();
    assert(data->InternalFieldCount() > 0);
    WatsonTTS* _this = reinterpret_cast<WatsonTTS*>(data->GetAlignedPointerFromInternalField(0));
    _this->mConn.Reset(info.GetIsolate(), info[0]->ToObject(ctx).ToLocalChecked());
}

v8::Local<v8::Function>
WatsonTTS::GetListeningFunction(v8::Isolate* isolate) {
    v8::EscapableHandleScope scope(isolate);
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    v8::Local<v8::ObjectTemplate> otmpl = v8::ObjectTemplate::New(isolate);
    otmpl->SetInternalFieldCount(1);
    v8::Local<v8::Object> obj = otmpl->NewInstance(ctx).ToLocalChecked();
    obj->SetAlignedPointerInInternalField(0, this);
    return scope.Escape(v8::Function::New(ctx, sListening, obj, 1).ToLocalChecked());
}

void
WatsonTTS::CreateSession() {
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
        v8::Local<v8::Value> cb[] = { Nan::New("connect").ToLocalChecked(), GetListeningFunction(isolate) };
        Nan::MakeCallback(session, "on", 2, cb);
    }
}

/*static*/
void WatsonTTS::sFreeHandle(uv_handle_t* handle) {
    delete handle;
}

/*static*/
void WatsonTTS::sCreateSession(uv_async_t* handle) {
    WatsonTTS* _this = (WatsonTTS*)handle->data;
    _this->CreateSession();
    uv_close(reinterpret_cast<uv_handle_t*>(handle), sFreeHandle);
}

/*static*/
void WatsonTTS::sWrite(uv_async_t* handle) {
    WriteData* wd = (WriteData*)handle->data;
    wd->mThis->Write(wd);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), sFreeHandle);
}

void
WatsonTTS::WSConnect() {
    uv_async_t* async = new uv_async_t;
    async->data = this;
    uv_async_init(uv_default_loop(), async,
            sCreateSession);
    uv_async_send(async);
}

void
WatsonTTS::Stop() {
    uv_async_t* async = new uv_async_t;
    async->data = this;
    uv_async_init(uv_default_loop(), async,
            sCloseSession);
    uv_async_send(async);
}

/*static*/
void WatsonTTS::sCloseSession(uv_async_t* handle) {
    WatsonTTS* _this = (WatsonTTS*)handle->data;
    _this->CloseSession();
    uv_close(reinterpret_cast<uv_handle_t*>(handle), sFreeHandle);
}

void
WatsonTTS::CloseSession() {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);
    if ( mConn.IsEmpty() ) {
        return;
    }
    v8::Local<v8::Object> conn = v8::Local<v8::Object>::New(isolate, mConn);
    Nan::MakeCallback(conn, "close", 0, NULL);
    mObj.Reset();
}

void
WatsonTTS::Write(WriteData* data) {
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
void WatsonTTS::sFreeCallback(char* data, void* hint) {
    WriteData* wd = (WriteData*)hint;
    delete wd;
}

void
WatsonTTS::SendVoiceWav(char* data, uint32_t length) {
    if ( length > 0 ) {
        uv_async_t* async = new uv_async_t;
        async->data = new WriteData(data, length, this, true);
        uv_async_init(uv_default_loop(), async,
                sWrite);
        uv_async_send(async);
    }
}

void
WatsonTTS::SendSilentWav() {
    uv_async_t* async = new uv_async_t;
    async->data = new WriteData(mSilent, ONE_SEC_FRAMES*2, this, false);
    uv_async_init(uv_default_loop(), async,
            sWrite);
    uv_async_send(async);
}
