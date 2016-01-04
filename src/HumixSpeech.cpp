/*******************************************************************************
* Copyright (c) 2015 IBM Corp.
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

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <stdint.h>

#include <fstream>
#include <queue>
#include <string>

#include <nan.h>
#include <sys/select.h>
#include <sndfile.h>
#include <alsa/asoundlib.h>
#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <pocketsphinx.h>

static char RIF_MARKER[5] = "RIFF";
static char WAVE[5] = "WAVE";
static char FMT[5] = "fmt ";

/* Sleep for specified msec */
static void sleep_msec(int32 ms) {
    /* ------------------- Unix ------------------ */
    struct timeval tmo;

    tmo.tv_sec = 0;
    tmo.tv_usec = ms * 1000;

    select(0, NULL, NULL, NULL, &tmo);
}

/**
 *
 * Use this class to write wav file
 * fixed format: S16, because sphinx uses it
 */
class WavWriter {
public:
    WavWriter(const char* filename, uint16_t channel, uint32_t sample) {
        mRIFFMarker = RIF_MARKER;
        mFiletypeHeader = WAVE;
        mFormatMarker = FMT;
        mDataHeaderLength = 16;
        mFileSize = 36;
        mFormatType = 1;
        mNumberOfChannels = channel;
        mSampleRate = sample;
        mBytesPerSecond = sample * channel * 16 / 8;
        mBytesPerFrame = channel * 16 / 8;
        mBitsPerSample = 16;
        mFilename = strdup(filename);
        ofs.open(mFilename, std::ofstream::out | std::ofstream::trunc);
    }

    ~WavWriter() {
        if (mFileSize > 36) {
            //modify the fie size field
            ofs.seekp(4);
            ofs.write((char*) &mFileSize, 4);

            ofs.seekp(40);
            uint32_t data_size = mFileSize - 36;
            ofs.write((char*) &data_size, 4);
        }
        ofs.close();
        free(mFilename);
    }

    void writeHeader();
    void writeData(const char *data, size_t size);

private:
    char* mFilename;
    char* mRIFFMarker;
    uint32_t mFileSize;
    char* mFiletypeHeader;
    char* mFormatMarker;
    uint32_t mDataHeaderLength;
    uint16_t mFormatType;
    uint16_t mNumberOfChannels;
    uint32_t mSampleRate;
    uint32_t mBytesPerSecond;
    uint16_t mBytesPerFrame;
    uint16_t mBitsPerSample;
    std::ofstream ofs;
};

void WavWriter::writeHeader() {
    ofs.write(mRIFFMarker, 4);
    ofs.write((char*) &mFileSize, 4);
    ofs.write(mFiletypeHeader, 4);
    ofs.write(mFormatMarker, 4);
    ofs.write((char*) &mDataHeaderLength, 4);
    ofs.write((char*) &mFormatType, 2);
    ofs.write((char*) &mNumberOfChannels, 2);
    ofs.write((char*) &mSampleRate, 4);
    ofs.write((char*) &mBytesPerSecond, 4);
    ofs.write((char*) &mBytesPerFrame, 2);
    ofs.write((char*) &mBitsPerSample, 2);
    ofs.write("data", 4);

    uint32_t data_size = mFileSize - 36;
    ofs.write((char*) &data_size, 4);
}

void WavWriter::writeData(const char* data, size_t size) {
    ofs.write(data, size);
    mFileSize += size;
}

class WavPlayer {
public:
    WavPlayer(const char *filename) {
        int pcm = 0;
        int dir = 0;
        snd_pcm_hw_params_t *params;
        mHandle = NULL;
        mBuff = NULL;
        mError = false;
        mSize = 0;
        mFile = NULL;

        SF_INFO sfinfo;
        mFile = sf_open(filename, SFM_READ, &sfinfo);
        if (!mFile) {
            printf("ERROR: %s\n", sf_strerror(NULL));
            mError = true;
            return;
        }

        /* Open the PCM device in playback mode */
        if ((pcm = snd_pcm_open(&mHandle, "default", SND_PCM_STREAM_PLAYBACK, 0))
                < 0) {
            printf("ERROR: %s\n", snd_strerror(pcm));
            mError = true;
            return;
        }
        /* Allocate the snd_pcm_hw_params_t structure on the stack. */
        snd_pcm_hw_params_alloca(&params);
        /* Init hwparams with full configuration space */
        if (snd_pcm_hw_params_any(mHandle, params) < 0) {
            printf("Can not configure this PCM device.\n");
            mError = true;
            return;
        }
        if (snd_pcm_hw_params_set_access(mHandle, params,
                SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
            printf("Error setting access.\n");
            mError = true;
            return;
        }

        /* Set sample format: always S16_LE */
        if (snd_pcm_hw_params_set_format(mHandle, params, SND_PCM_FORMAT_S16_LE)
                < 0) {
            printf("Error setting format.\n");
            return;
        }

        /* Set sample rate */
        uint32_t exact_rate = sfinfo.samplerate;
        if (snd_pcm_hw_params_set_rate_near(mHandle, params, &exact_rate, &dir)
                < 0) {
            printf("Error setting rate.\n");
            mError = true;
            return;
        }
        /* Set number of channels */
        if (snd_pcm_hw_params_set_channels(mHandle, params, sfinfo.channels)
                < 0) {
            printf("Error setting channels.\n");
            mError = true;
            return;
        }
        /* Write the parameters to the driver */
        if ((pcm = snd_pcm_hw_params(mHandle, params)) < 0) {
            printf("unable to set hw parameters: %s\n", snd_strerror(pcm));
            mError = true;
            return;
        }

        /* Use a buffer large enough to hold one period */
        if (snd_pcm_hw_params_get_period_size(params, &mFrames, NULL) < 0) {
            printf("Error get buffer size.\n");
            mError = true;
            return;
        }
        snd_pcm_nonblock(mHandle, 0);
//        printf("frames:%lu\n", mFrames);

        mSize = mFrames * sfinfo.channels * 2; /* 2 -> sample size */
        ;
        mBuff = (char *) malloc(mSize);
    }

    ~WavPlayer() {
        if (!mError) {
            if (mHandle) {
                snd_pcm_drain(mHandle);
                snd_pcm_close(mHandle);
            }
            if (mBuff) {
                free(mBuff);
            }
        }
    }

    void play() {
        if (mFile) {
            int pcmrc = 0;
            int readcount = 0;
            while ((readcount = sf_readf_short(mFile, (short*) mBuff, mFrames))
                    > 0) {
                pcmrc = snd_pcm_writei(mHandle, mBuff, readcount);
                if (pcmrc == -EPIPE) {
                    printf("Underrun!\n");
                    snd_pcm_recover(mHandle, pcmrc, 1);
                } else if (pcmrc != readcount) {
                    printf("wframe count mismatched: %d, %d\n", pcmrc,
                            readcount);
                }
            }
            sf_close(mFile);
            mFile = NULL;
        }
    }
private:
    snd_pcm_t *mHandle;
    SNDFILE *mFile;
    char* mBuff;
    size_t mSize;
    bool mError;
    snd_pcm_uframes_t mFrames;
};

static const arg_t cont_args_def[] = {
    POCKETSPHINX_OPTIONS,
    /* Argument file. */
    {"-argfile",
     ARG_STRING,
     NULL,
     "Argument file giving extra arguments."},
    {"-adcdev",
     ARG_STRING,
     NULL,
     "Name of audio device to use for input."},
    {"-time",
     ARG_BOOLEAN,
     "no",
     "Print word times in file transcription."},
    {"-cmdproc",
     ARG_STRING,
     "./processcmd.sh",
     "command processor."},
    {"-wav-say",
     ARG_STRING,
     "./voice/interlude/pleasesay1.wav",
     "the wave file of saying."},
    {"-wav-proc",
     ARG_STRING,
     "./voice/interlude/process1.wav",
     "the wave file of processing."},
    {"-wav-bye",
     ARG_STRING,
     "./voice/interlude/bye.wav",
     "the wave file of goodbye."},
    {"-lang",
     ARG_STRING,
     "zh-tw",
     "language locale."},

    CMDLN_EMPTY_OPTION
};

class WatsonTTS {
public:
    WatsonTTS(const char* username, const char* passwd, v8::Local<v8::Function> func);
    ~WatsonTTS();

    class WriteData {
    public:
        WriteData(const char* data, uint32_t size, WatsonTTS* tts) {
            mData = (char*)malloc(size);
            memcpy(mData, data, size);
            mSize = size;
            mThis = tts;
        }
        ~WriteData() {
            free (mData);
        }
        char* mData;
        uint32_t mSize;
        WatsonTTS* mThis;
    };
    void WSConnect();
    void setCB(v8::Local<v8::Function> cb) {
        mCB.Reset(cb->CreationContext()->GetIsolate(), cb);
    }
    void stop();
    void write(WriteData* data);
    void writeToMainLoop(char* data, uint32_t length);
private:

    static void sStart(uv_async_t* handle);
    static void sWrite(uv_async_t* handle);
    static void sFreeHandle(uv_handle_t* handle);
    static void sFreeCallback(char* data, void* hint);
    void connect();

    char* mUserName;
    char* mPasswd;
    v8::Persistent<v8::Object> mObj;
    v8::Persistent<v8::Function> mFunc;
    v8::Persistent<v8::Function> mCB;
};

WatsonTTS::WatsonTTS(const char* username, const char* passwd, v8::Local<v8::Function> func)
    : mUserName(strdup(username)), mPasswd(strdup(passwd)){
    mFunc.Reset(func->CreationContext()->GetIsolate(), func);
}

WatsonTTS::~WatsonTTS() {
    free(mUserName);
    free(mPasswd);
    mObj.Reset();
    mFunc.Reset();
    mCB.Reset();
}

void
WatsonTTS::connect() {
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
        mObj.Reset(isolate, rev->ToObject(ctx).ToLocalChecked());
    }
    //TODO register 'connect' event to get connection object
    //then use it when calling the stop()
}

/*static*/
void WatsonTTS::sFreeHandle(uv_handle_t* handle) {
    delete handle;
}

/*static*/
void WatsonTTS::sStart(uv_async_t* handle) {
    WatsonTTS* _this = (WatsonTTS*)handle->data;
    _this->connect();
    uv_close(reinterpret_cast<uv_handle_t*>(handle), sFreeHandle);
}

/*static*/
void WatsonTTS::sWrite(uv_async_t* handle) {
    WriteData* wd = (WriteData*)handle->data;
    wd->mThis->write(wd);
    uv_close(reinterpret_cast<uv_handle_t*>(handle), sFreeHandle);
}

void
WatsonTTS::WSConnect() {
    uv_async_t* async = new uv_async_t;
    async->data = this;
    uv_async_init(uv_default_loop(), async,
            sStart);
    uv_async_send(async);
}

void
WatsonTTS::write(WriteData* data) {
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
WatsonTTS::writeToMainLoop(char* data, uint32_t length) {
    uv_async_t* async = new uv_async_t;
    async->data = new WriteData(data, length, this);
    uv_async_init(uv_default_loop(), async,
            sWrite);
    uv_async_send(async);
}

class HumixSpeech : public Nan::ObjectWrap{
public:
    HumixSpeech(const v8::FunctionCallbackInfo<v8::Value>& args);
    ~HumixSpeech();

    typedef enum {
        kReady,
        kKeyword,
        kWaitCommand,
        kCommand,
        kStop
    } State;

    static v8::Local<v8::FunctionTemplate> sFunctionTemplate(
            v8::Isolate* isolate);
private:
    static void sV8New(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void sStart(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void sPlay(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void sStop(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void sEnableWatson(const v8::FunctionCallbackInfo<v8::Value>& info);

    void start(const v8::FunctionCallbackInfo<v8::Value>& info);
    void stop(const v8::FunctionCallbackInfo<v8::Value>& info);
    void play(const v8::FunctionCallbackInfo<v8::Value>& info);

    static void sLoop(void* arg);
    int processCommand(char* msg, int len);

    static void sReceiveCmd(uv_async_t* handle);
    static void sFreeHandle(uv_handle_t* handle);

    State mState;
    ps_decoder_t *mPSDecoder;
    cmd_ln_t *mConfig;
    char* mCMDProc;
    char* mWavSay;
    char* mWavProc;
    char* mWavBye;
    char* mLang;
    char* mSampleRate;
    int mArgc;
    char** mArgv;
    uv_thread_t mThread;
    uv_mutex_t mAplayMutex;
    uv_mutex_t mCommandMutex;
    v8::Persistent<v8::Function> mCB;
    std::queue<std::string> mAplayFiles;
    std::queue<std::string> mCommands;
    WatsonTTS* mWatsonTTS;
};

static char* sGetObjectPropertyAsString(
        v8::Local<v8::Context> ctx,
        v8::Local<v8::Object> &obj,
        const char* name,
        const char* defaultValue) {

    v8::Local<v8::Value> valObj;
    if ( obj->Get(ctx, Nan::New(name).ToLocalChecked()).ToLocal(&valObj) &&
            !valObj->IsUndefined() &&
            !valObj->IsNull()) {
        v8::String::Utf8Value val(valObj);
        return strdup(*val);
    } else {
        return strdup(defaultValue);
    }
}

HumixSpeech::HumixSpeech(const v8::FunctionCallbackInfo<v8::Value>& args)
        : mThread(0) {
    v8::Local<v8::Object> config = args[0]->ToObject();
    v8::Local<v8::Context> ctx = args.GetIsolate()->GetCurrentContext();
    mState = kReady;

    mCMDProc = sGetObjectPropertyAsString(ctx, config, "cmdproc", "./util/processcmd.sh");
    mWavSay =  sGetObjectPropertyAsString(ctx, config, "wav-say", "./voice/interlude/pleasesay1.wav");
    mWavProc =  sGetObjectPropertyAsString(ctx, config, "wav-say", "./voice/interlude/process1.wav");
    mWavBye =  sGetObjectPropertyAsString(ctx, config, "wav-bye", "./voice/interlude/bye.wav");
    mLang =  sGetObjectPropertyAsString(ctx, config, "lang", "zh-tw");
    mSampleRate =  sGetObjectPropertyAsString(ctx, config, "samprate", "16000");

    char const *cfg;
    v8::Local<v8::Array> props = config->GetPropertyNames();
    int propsNum = props->Length();
    mArgc = propsNum * 2;
    mArgv = (char**)calloc(mArgc, sizeof(char**));
    int counter = 0;
    for ( int i = 0; i < propsNum; i++ ) {
        v8::Local<v8::Value> valObj;
        if ( props->Get(ctx, i).ToLocal(&valObj) ) {
            //option: need to add '-' prefix as an option
            v8::String::Utf8Value name(valObj);
            char** p = mArgv + counter++;
            *p = (char*)malloc(name.length() + 2);
            sprintf(*p, "-%s", *name);
            if ( config->Get(ctx, valObj).ToLocal(&valObj) &&
                    !valObj->IsNull() &&
                    !valObj->IsUndefined()) {
                //option value
                v8::String::Utf8Value val(valObj);
                p = mArgv + counter++;
                *p = strdup(*val);
            }
        }
    }
    mConfig = cmd_ln_parse_r(NULL, cont_args_def, mArgc, mArgv, TRUE);

    /* Handle argument file as -argfile. */
    if (mConfig && (cfg = cmd_ln_str_r(mConfig, "-argfile")) != NULL) {
        mConfig = cmd_ln_parse_file_r(mConfig, cont_args_def, cfg, FALSE);
    }

    ps_default_search_args(mConfig);
    mPSDecoder = ps_init(mConfig);
    if (mPSDecoder == NULL) {
        cmd_ln_free_r(mConfig);
        mConfig = NULL;
        args.GetIsolate()->ThrowException(v8::Exception::Error(Nan::New("Can't initialize ps").ToLocalChecked()));
    }
    uv_mutex_init(&mAplayMutex);
    uv_mutex_init(&mCommandMutex);
    mWatsonTTS = NULL;
    Wrap(args.This());
}

HumixSpeech::~HumixSpeech() {
    if ( mPSDecoder )
        ps_free(mPSDecoder);
    if ( mConfig)
        cmd_ln_free_r(mConfig);
    if (mCMDProc)
        free(mCMDProc);
    if (mWavSay)
        free(mWavSay);
    if (mWavProc)
        free(mWavProc);
    if (mWavBye)
        free(mWavBye);
    if (mSampleRate)
        free(mSampleRate);
    if (mLang)
        free(mLang);

    if ( mArgv ) {
        for ( int i = 0; i < mArgc; i++ ) {
            if ( mArgv[i] ) {
                free(mArgv[i]);
            }
        }
        free(mArgv);
    }
    uv_mutex_destroy(&mAplayMutex);
    uv_mutex_destroy(&mCommandMutex);
    if ( mWatsonTTS ) {
        delete mWatsonTTS;
    }
    mCB.Reset();
}

/*static*/
void HumixSpeech::sV8New(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    if ( info.Length() != 1 ) {
        info.GetIsolate()->ThrowException(
                v8::Exception::SyntaxError(Nan::New("one argument").ToLocalChecked()));
        return info.GetReturnValue().Set(v8::Undefined(isolate));
    }

    v8::Local<v8::Object> configObj = info[0]->ToObject();

    if ( configObj.IsEmpty() ) {
        info.GetIsolate()->ThrowException(
                v8::Exception::SyntaxError(Nan::New("The first argument shall be an object").ToLocalChecked()));
        return info.GetReturnValue().Set(v8::Undefined(isolate));
    }

    new HumixSpeech(info);
    return info.GetReturnValue().Set(info.This());
}

/*static*/
void HumixSpeech::sStart(const v8::FunctionCallbackInfo<v8::Value>& info) {
    HumixSpeech* hs = Unwrap<HumixSpeech>(info.Holder());
    if ( hs == nullptr ) {
        info.GetIsolate()->ThrowException(v8::Exception::ReferenceError(
                Nan::New("Not a HumixSpeech object").ToLocalChecked()));
        return;
    }

    if ( info.Length() < 1 || !info[0]->IsFunction() ) {
        info.GetIsolate()->ThrowException(v8::Exception::SyntaxError(
                Nan::New("Usage: start(callback)").ToLocalChecked()));
        return;
    }
    hs->start(info);
}

void
HumixSpeech::start(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Local<v8::Function> cb = info[0].As<v8::Function>();
    if ( mWatsonTTS ) {
        mWatsonTTS->setCB(cb);
    }
    mCB.Reset(info.GetIsolate(), cb);
    uv_thread_create(&mThread, sLoop, this);
}

/*static*/
void HumixSpeech::sStop(const v8::FunctionCallbackInfo<v8::Value>& info) {
    HumixSpeech* hs = Unwrap<HumixSpeech>(info.Holder());
    if ( hs == nullptr ) {
        info.GetIsolate()->ThrowException(v8::Exception::ReferenceError(
                Nan::New("Not a HumixSpeech object").ToLocalChecked()));
        return;
    }

    if ( info.Length() != 0 ) {
        info.GetIsolate()->ThrowException(v8::Exception::SyntaxError(
                Nan::New("Usage: stop()").ToLocalChecked()));
        return;
    }
    hs->stop(info);
}

void
HumixSpeech::stop(const v8::FunctionCallbackInfo<v8::Value>& info) {
    mCB.Reset();
    mState = kStop;
    uv_thread_join(&mThread);
}

/*static*/
void HumixSpeech::sPlay(const v8::FunctionCallbackInfo<v8::Value>& info) {
    HumixSpeech* hs = Unwrap<HumixSpeech>(info.Holder());
    if ( hs == nullptr ) {
        info.GetIsolate()->ThrowException(v8::Exception::ReferenceError(
                Nan::New("Not a HumixSpeech object").ToLocalChecked()));
        return;
    }

    if ( info.Length() != 1 || !info[0]->IsString()) {
        info.GetIsolate()->ThrowException(v8::Exception::SyntaxError(
                Nan::New("Usage: play(filename)").ToLocalChecked()));
        return;
    }
    hs->play(info);
}

void
HumixSpeech::play(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::String::Utf8Value filename(info[0]);
    uv_mutex_lock(&mAplayMutex);
    mAplayFiles.push(*filename);
    uv_mutex_unlock(&mAplayMutex);
}

/*static*/
void HumixSpeech::sEnableWatson(const v8::FunctionCallbackInfo<v8::Value>& info) {
    HumixSpeech* hs = Unwrap<HumixSpeech>(info.Holder());
    if ( hs == nullptr ) {
        info.GetIsolate()->ThrowException(v8::Exception::ReferenceError(
                Nan::New("Not a HumixSpeech object").ToLocalChecked()));
        return;
    }

    if ( info.Length() != 3 || !info[0]->IsString() ||
            !info[1]->IsString() || !info[2]->IsFunction()) {
        info.GetIsolate()->ThrowException(v8::Exception::SyntaxError(
                Nan::New("Usage: enableWatson(username, passwd, function)").ToLocalChecked()));
        return;
    }

    v8::String::Utf8Value username(info[0]);
    v8::String::Utf8Value passwd(info[1]);
    hs->mWatsonTTS = new WatsonTTS(*username, *passwd, info[2].As<v8::Function>());
}

/*static*/
void HumixSpeech::sLoop(void* arg) {
    HumixSpeech* _this = (HumixSpeech*)arg;
    ps_decoder_t* ps = _this->mPSDecoder;
    ad_rec_t *ad;
    int16 adbuf[2048];
    size_t adbuflen = 2048;
    uint8 in_speech; //utt_started, in_speech;
    int32 k;
    char const *hyp;

    _this->mState = kReady;
    int waitCount = 0;
    int humixCount = 0;
    int samprate = (int) cmd_ln_float32_r(_this->mConfig, "-samprate");

    WavWriter *wavWriter = NULL;

    if ((ad = ad_open_dev(cmd_ln_str_r(_this->mConfig, "-adcdev"), samprate)) == NULL) {
        E_FATAL("Failed to open audio device\n");
    }
    if (ad_start_rec(ad) < 0) {
        E_FATAL("Failed to start recording\n");
    }

    if (ps_start_utt(_this->mPSDecoder) < 0) {
        E_FATAL("Failed to start utterance\n");
    }
    //utt_started = FALSE;
    printf("READY....\n");

    for (; _this->mState != kStop ;) {
        //read data from rec, we need to check if there is any aplay request from node first
        if (_this->mState != kCommand) {
            uv_mutex_lock(&(_this->mAplayMutex));
            if (!_this->mAplayFiles.empty()) {
                ad_stop_rec(ad);
                std::string file = _this->mAplayFiles.front();
                printf("receive aplay command:%s\n", file.c_str());
                {
                    WavPlayer player(file.c_str());
                    player.play();
                }
                ad_start_rec(ad);
            }
            uv_mutex_unlock(&(_this->mAplayMutex));
        }
        if ((k = ad_read(ad, adbuf, adbuflen)) < 0)
            E_FATAL("Failed to read audio\n");

        //start to process the data we got from rec
        ps_process_raw(ps, adbuf, k, FALSE, FALSE);
        in_speech = ps_get_in_speech(ps);

        switch (_this->mState) {
            case kReady:
                humixCount = 0;
                if (in_speech) {
                    _this->mState = kKeyword;
                    printf("Waiting for keyward: HUMIX... \n");
                }
                break;
            case kKeyword:
                if (in_speech) {
                    //keep receiving keyword
                } else {
                    //keyword done
                    ps_end_utt(ps);
                    hyp = ps_get_hyp(ps, NULL);
                    if (hyp != NULL && strcmp("HUMIX", hyp) == 0) {
                        _this->mState = kWaitCommand;
                        printf("keyword HUMIX found\n");
                        fflush(stdout);
                        if (_this->mWatsonTTS ) {
                            _this->mWatsonTTS->WSConnect();
                        }
                        ad_stop_rec(ad);
                        {
                            WavPlayer player(_this->mWavSay);
                            player.play();
                        }
                        ad_start_rec(ad);
                        printf("Waiting for a command...");
                        humixCount = 0;
                    } else {
                        _this->mState = kReady;
                        printf("READY....\n");
                    }
                    if (ps_start_utt(ps) < 0)
                        E_FATAL("Failed to start utterance\n");
                }
                break;
            case kWaitCommand:
                if (in_speech) {
                    printf("Listening the command...\n");
                    _this->mState = kCommand;
                    if ( _this->mWatsonTTS ) {
                        _this->mWatsonTTS->writeToMainLoop((char*) adbuf, (uint32_t) (k * 2));
                    } else {
                        wavWriter = new WavWriter("/dev/shm/test.wav", 1, samprate);
                        wavWriter->writeHeader();
                        wavWriter->writeData((char*) adbuf, (size_t) (k * 2));
                    }
                } else {
                    //increase waiting count;
                    if (++waitCount > 100) {
                        waitCount = 0;
                        if (++humixCount > 20) {
                            //exit humix-loop
                            _this->mState = kReady;
                            printf("READY....\n");
                        } else {
                            printf(".");
                        }
                        ps_end_utt(ps);
                        if (ps_start_utt(ps) < 0)
                            E_FATAL("Failed to start utterance\n");
                    }
                }
                break;
            case kCommand:
                if (in_speech) {
                    //keep receiving command
                    if ( _this->mWatsonTTS ) {
                        _this->mWatsonTTS->writeToMainLoop((char*) adbuf, (uint32_t) (k * 2));
                    } else {
                        wavWriter->writeData((char*) adbuf, (size_t) (k * 2));
                    }
                } else {
                    if ( _this->mWatsonTTS ) {
                        _this->mWatsonTTS->writeToMainLoop((char*) adbuf, (uint32_t) (k * 2));
                    } else {
                        wavWriter->writeData((char*) adbuf, (size_t) (k * 2));
                        delete wavWriter;
                        wavWriter = NULL;
                    }
                    //start to process command
                    ps_end_utt(ps);
                    printf("StT processing\n");
                    char msg[1024];
                    ad_stop_rec(ad);
                    {
                        WavPlayer player(_this->mWavProc);
                        player.play();
                    }
                    if ( !_this->mWatsonTTS ) {
                        int result = _this->processCommand(msg, 1024);
                        if (result == 0) {
                            uv_async_t* async = new uv_async_t;
                            async->data = _this;
                            uv_mutex_lock(&(_this->mCommandMutex));
                            _this->mCommands.push(msg);
                            uv_mutex_unlock(&(_this->mCommandMutex));
                            uv_async_init(uv_default_loop(), async,
                                    HumixSpeech::sReceiveCmd);
                            uv_async_send(async);
                        } else {
                            printf("No command found!");
                        }
                    }
                    //once we got command, reset humix-loop
                    humixCount = 0;
                    ad_start_rec(ad);
                    _this->mState = kWaitCommand;
                    printf("Waiting for a command...\n");
                    if (ps_start_utt(ps) < 0)
                        E_FATAL("Failed to start utterance\n");
                }
                break;
            case kStop:
                break;
        }

        sleep_msec(20);
    }
    ad_close(ad);
}

/*static*/
void HumixSpeech::sReceiveCmd(uv_async_t* async) {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();

    HumixSpeech* _this = (HumixSpeech*)async->data;
    if ( !_this->mCB.IsEmpty() ) {
        uv_mutex_lock(&(_this->mCommandMutex));
        while( _this->mCommands.size() > 0) {
            std::string cmd = _this->mCommands.front();
            _this->mCommands.pop();
            v8::Local<v8::Value> argv[] = { Nan::New(cmd.c_str()).ToLocalChecked() };
            v8::Local<v8::Function> func = v8::Local<v8::Function>::New(isolate, _this->mCB);
            func->CallAsFunction(ctx, ctx->Global(), 1, argv);
        }
        uv_mutex_unlock(&(_this->mCommandMutex));
    }

    uv_close(reinterpret_cast<uv_handle_t*>(async), HumixSpeech::sFreeHandle);
}

/*static*/
void
HumixSpeech::sFreeHandle(uv_handle_t* handle) {
    delete handle;
}

/*static*/
v8::Local<v8::FunctionTemplate> HumixSpeech::sFunctionTemplate(
        v8::Isolate* isolate) {
    v8::EscapableHandleScope scope(isolate);
    v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate,
            HumixSpeech::sV8New);
    tmpl->SetClassName(Nan::New("HumixSpeech").ToLocalChecked());
    tmpl->InstanceTemplate()->SetInternalFieldCount(1);
    NODE_SET_PROTOTYPE_METHOD(tmpl, "start", sStart);
    NODE_SET_PROTOTYPE_METHOD(tmpl, "stop", sStop);
    NODE_SET_PROTOTYPE_METHOD(tmpl, "play", sPlay);
    NODE_SET_PROTOTYPE_METHOD(tmpl, "enableWatson", sEnableWatson);

    return scope.Escape(tmpl);
}

int HumixSpeech::processCommand(char* msg, int len) {
    int filedes[2];
    if (pipe(filedes) == -1) {
        printf("pipe error");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    int status;
    if (pid == 0) {
        while ((dup2(filedes[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {
        }
        close(filedes[1]);
        close(filedes[0]);
        int rev = execl(mCMDProc, "processcmd.sh", "/dev/shm/test.wav",
                "/dev/shm/test.flac", mLang, mSampleRate, (char*) NULL);
        if (rev == -1) {
            printf("fork error:%s\n", strerror(errno));
        }
        _exit(1);
    } else {
        waitpid(pid, &status, 0);
    }
    close(filedes[1]);
    if (status == 0 && msg && len > 0) {
        ssize_t outlen = read(filedes[0], msg, len - 1);
        if (outlen > 0) {
            msg[outlen] = 0;
        }
    }
    close(filedes[0]);
    return status;
}



void InitModule(v8::Local<v8::Object> target) {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    Nan::HandleScope scope;
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();

    v8::Local<v8::FunctionTemplate> ft = HumixSpeech::sFunctionTemplate(isolate);

    target->Set(ctx, Nan::New("HumixSpeech").ToLocalChecked(),
            ft->GetFunction(ctx).ToLocalChecked());
}

NODE_MODULE(HumixSpeech, InitModule);
