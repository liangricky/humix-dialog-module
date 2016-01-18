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

#include "NaoSpeech.hpp"

AL::ALTextToSpeechProxy* NaoSpeech::sSpeechProxy = NULL;
uv_mutex_t NaoSpeech::sAlSpeechQueueMutex;

/*static*/
void NaoSpeech::sInitNaoSpeech(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if ( info.Length() != 1 && !info[0]->IsObject()) {
        info.GetIsolate()->ThrowException(
                v8::Exception::Error(Nan::New("usage: initNaoSpeech(option)").ToLocalChecked()));
        return;
    }
    v8::Local<v8::Context> ctx = info.GetIsolate()->GetCurrentContext();

    v8::Local<v8::Object> option = info[0]->ToObject(ctx).ToLocalChecked();

    if ( !sSpeechProxy ) {
        //TODO: fixme
        sSpeechProxy = new AL::ALTextToSpeechProxy("nao.local" , 9559);
        uv_mutex_init(&sAlSpeechQueueMutex);
    }
    uv_mutex_lock(&sAlSpeechQueueMutex);

    //pitchShift  Acceptable range is [1.0 - 4]. 0 disables the effect.
    //doubleVoice  Acceptable range is [1.0 - 4]. 0 disables the effect.
    //doubleVoiceLevel  Acceptable range is [0 - 4]. 0 disables the effect.
    //doubleVoiceTimeShift  Acceptable range is [0 - 0.5].
    v8::Local<v8::String> pitchStr = Nan::New("pitchShift").ToLocalChecked();
    v8::Local<v8::String> doubleVliceStr = Nan::New("doubleVoice").ToLocalChecked();
    v8::Local<v8::String> doubleVoiceLevelStr = Nan::New("doubleVoiceLevel").ToLocalChecked();
    v8::Local<v8::String> doubleVoliceTimeStr = Nan::New("doubleVoiceTimeShift").ToLocalChecked();
    if ( option->Has(pitchStr) ) {
        v8::Local<v8::Value> value = option->Get(ctx, pitchStr).ToLocalChecked();
        if ( value->IsNumber() ) {
            v8::Local<v8::Number> number = value->ToNumber(ctx).ToLocalChecked();
            sSpeechProxy->setParameter("pitchShift", (float)number->Value());
        }
    }
    if ( option->Has(doubleVliceStr) ) {
        v8::Local<v8::Value> value = option->Get(ctx, doubleVliceStr).ToLocalChecked();
        if ( value->IsNumber() || value->IsNumberObject() ) {
            v8::Local<v8::Number> number = value->ToNumber(ctx).ToLocalChecked();
            sSpeechProxy->setParameter("doubleVoice", (float)number->Value());
        }
    }
    if ( option->Has(doubleVoiceLevelStr) ) {
        v8::Local<v8::Value> value = option->Get(ctx, doubleVoiceLevelStr).ToLocalChecked();
        if ( value->IsNumber() || value->IsNumberObject() ) {
            v8::Local<v8::Number> number = value->ToNumber(ctx).ToLocalChecked();
            sSpeechProxy->setParameter("doubleVoiceLevel", (float)number->Value());
        }
    }
    if ( option->Has(doubleVoliceTimeStr) ) {
        v8::Local<v8::Value> value = option->Get(ctx, doubleVoliceTimeStr).ToLocalChecked();
        if ( value->IsNumber() || value->IsNumberObject() ) {
            v8::Local<v8::Number> number = value->ToNumber(ctx).ToLocalChecked();
            sSpeechProxy->setParameter("doubleVoiceTimeShift", (float)number->Value());
        }
    }
    uv_mutex_unlock(&sAlSpeechQueueMutex);
}

/*static*/
void NaoSpeech::sSay(const char* str) {
    if ( sSpeechProxy ){
        uv_mutex_lock(&sAlSpeechQueueMutex);
        sSpeechProxy->say(str);
        uv_mutex_unlock(&sAlSpeechQueueMutex);
    }
}

void InitModule(v8::Local<v8::Object> target) {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    Nan::HandleScope scope;
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();

    target->Set(ctx, Nan::New("initNaoSpeech").ToLocalChecked(),
            v8::Function::New(ctx, NaoSpeech::sInitNaoSpeech, v8::Local<v8::Value>(), 0).ToLocalChecked());
}

NODE_MODULE(NaoSpeech, InitModule);
