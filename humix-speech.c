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

#include <sys/select.h>
#include <sndfile.h>
#include <alsa/asoundlib.h>
#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <pocketsphinx.h>

static char RIF_MARKER[5] = "RIFF";
static char WAVE[5] = "WAVE";
static char FMT[5] = "fmt ";

/**
 *
 * Use this class to write wav file
 * fixed format: S16, because sphinx uses it
 */

class WavWriter
{
public:
    WavWriter(const char* filename, uint16_t channel, uint32_t sample) {
        RIFF_marker = RIF_MARKER;
        filetype_header = WAVE;
        format_marker = FMT;
        data_header_length = 16;
        file_size = 36;
        format_type = 1;
        number_of_channels = channel;
        sample_rate = sample;
        bytes_per_second = sample * channel * 16 / 8;
        bytes_per_frame = channel * 16 / 8;
        bits_per_sample = 16;
        ofs.open (filename, std::ofstream::out | std::ofstream::trunc);
    }

    ~WavWriter() {
        if (file_size>36) {
            //modify the fie size field
            ofs.seekp(4);
            ofs.write((char*)&file_size, 4);

            ofs.seekp(40);
            uint32_t data_size = file_size - 36;
            ofs.write((char*)&data_size, 4);
        }
        ofs.close();
    }

    void writeHeader();
    void writeData(const char *data, size_t size);

private:
    char* filename;
    char* RIFF_marker;
    uint32_t file_size;
    char* filetype_header;
    char* format_marker;
    uint32_t data_header_length;
    uint16_t format_type;
    uint16_t number_of_channels;
    uint32_t sample_rate;
    uint32_t bytes_per_second;
    uint16_t bytes_per_frame;
    uint16_t bits_per_sample;
    std::ofstream ofs;
};

void WavWriter::writeHeader()
{
    ofs.write(RIFF_marker, 4);
    ofs.write((char*)&file_size, 4);
    ofs.write(filetype_header, 4);
    ofs.write(format_marker, 4);
    ofs.write((char*)&data_header_length, 4);
    ofs.write((char*)&format_type, 2);
    ofs.write((char*)&number_of_channels, 2);
    ofs.write((char*)&sample_rate, 4);
    ofs.write((char*)&bytes_per_second, 4);
    ofs.write((char*)&bytes_per_frame, 2);
    ofs.write((char*)&bits_per_sample, 2);
    ofs.write("data", 4);

    uint32_t data_size = file_size - 36;
    ofs.write((char*)&data_size, 4);
}

void WavWriter::writeData(const char* data, size_t size) {
    ofs.write(data, size);
    file_size += size;
}

class WavPlayer {
public:
    WavPlayer(uint16_t channel, uint32_t sample_rate) {
        unsigned int pcm = 0;
        int dir = 0;
        
        frames = 64;
        pcm_handle = NULL;
        params = NULL;
        buff = NULL;
        error = false;
        size = 0;

        /* Open the PCM device in playback mode */
        if ( (pcm = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
            printf("ERROR: %s\n", snd_strerror(pcm));
            error = true;
            return;
        }
        /* Allocate the snd_pcm_hw_params_t structure on the stack. */
        snd_pcm_hw_params_alloca(&params);
        /* Init hwparams with full configuration space */
        if (snd_pcm_hw_params_any(pcm_handle, params) < 0) {
            printf("Can not configure this PCM device.\n");
            error = true;
            return;
        }
        if (snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
            printf("Error setting access.\n");
            error = true;
            return;
        }

        /* Set sample format: always S16_LE */
        if (snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_S16_LE) < 0) {
            printf("Error setting format.\n");
            return;
        }
        /* Set sample rate */
        uint32_t exact_rate = sample_rate;
        if (snd_pcm_hw_params_set_rate_near(pcm_handle, params, &exact_rate, &dir) < 0) {
            printf("Error setting rate.\n");
            error = true;
            return;
        }
        /* Set number of channels */
        if (snd_pcm_hw_params_set_channels(pcm_handle, params, channel) < 0) {
            printf("Error setting channels.\n");
            error = true;
            return;
        }
        if (snd_pcm_hw_params_set_period_size_near(pcm_handle, params, &frames, &dir) < 0) {
            printf("Error setting period size.\n");
            error = true;
            return;
        }
        /* Write the parameters to the driver */
        if ( (pcm = snd_pcm_hw_params(pcm_handle, params)) < 0) {
            printf("unable to set hw parameters: %s\n",
              snd_strerror(pcm));
            error = true;
            return;
        }
        /* Use a buffer large enough to hold one period */
        if (snd_pcm_hw_params_get_period_size(params, &frames, NULL) < 0) {
            printf("Error get buffer size.\n");
            error = true;
            return;
        }

        size = frames * channel * 2; /* 2 -> sample size */;
        buff = (char *) malloc(size);

    }

    ~WavPlayer() {
        if (!error) {
            if ( pcm_handle ) {
                snd_pcm_close(pcm_handle);
            }
            if ( buff ) {
                free(buff);
            }
        }
    }

    void play(const char* filename) {
        SF_INFO sfinfo;
        SNDFILE *infile = NULL;
        infile = sf_open(filename, SFM_READ, &sfinfo);
        if (infile) {
            int pcmrc = 0;
            int readcount = 0;
             while ((readcount = sf_readf_short(infile, (short*)buff, frames))>0) {
                 //printf("readcount:%d\n", readcount);
                 pcmrc = snd_pcm_writei(pcm_handle, buff, readcount);
                 if (pcmrc == -EPIPE) {
                     fprintf(stderr, "Underrun!\n");
                     snd_pcm_prepare(pcm_handle);
                 }
             }
             sf_close(infile);
        }
    }
private:
    uint16_t channel;
    uint32_t sample_rate;
    snd_pcm_t *pcm_handle;
    snd_pcm_hw_params_t *params;
    snd_pcm_uframes_t frames;
    char* buff;
    size_t size;
    bool error;
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

typedef enum {
    kReady,
    kKeyword,
    kWaitCommand,
    kCommand
} State;

static State state;
static ps_decoder_t *ps;
static cmd_ln_t *config;
static char const* cmdproc = NULL;
static char const* wav_say = NULL;
static char const* wav_proc = NULL;
static char const* wav_bye = NULL;
static char const* lang = NULL;
static char samprateStr[10];

/* Sleep for specified msec */
static void
sleep_msec(int32 ms)
{
    /* ------------------- Unix ------------------ */
    struct timeval tmo;

    tmo.tv_sec = 0;
    tmo.tv_usec = ms * 1000;

    select(0, NULL, NULL, NULL, &tmo);
}

static size_t
strlcpy(char *dst, const char *src, size_t siz)
{
    register char *d = dst;
    register const char *s = src;
    register size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0 && --n != 0) {
            do {
                        if ((*d++ = *s++) == 0)
                            break;
                    } while (--n != 0);
        }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
            if (siz != 0)
                *d = '\0';      /* NUL-terminate dst */
            while (*s++)
                ;
        }

    return(s - src - 1);    /* count does not include NUL */
}

/*
 * connect to nodejs and use this channel to get aplay command
 */
static int sConnect2Node(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) return -1;

    int flags = fcntl (fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);//non-blocking

    struct sockaddr_un unaddr;
    unaddr.sun_family = AF_UNIX;
    strlcpy(unaddr.sun_path, path, sizeof(unaddr.sun_path));

    int attempts = 10;
    while (connect(fd, (struct sockaddr*)&unaddr, sizeof(unaddr)) < 0)
    {
        if ((attempts-- > 0) &&
          (errno == ENOENT || errno == EAGAIN))
        {
            struct pollfd pfd = { fd, POLLIN | POLLOUT, 0 };
            poll(&pfd, 1, 1 /* 1 ms */);
            if (!(pfd.revents & POLLERR))
                continue;
        }
        close(fd);
        return -1;
    }
    
    return fd;
}

static int sGetAplayCommand(int fd, char* buff, ssize_t len) {
    char msgLenBuff[4];
    uint32_t* msgLen;
    if ( fd > 0 ) {
        ssize_t readLen = read(fd, msgLenBuff, 4);
        if ( readLen == 4 ) {
            msgLen = (uint32_t*) msgLenBuff;
            char payload[*msgLen + 1];
            readLen = read(fd, payload, *msgLen);
            if ( readLen == (ssize_t)*msgLen ) {
                payload[*msgLen] = 0;
                strlcpy(buff, payload + 1, len);
                return 1;
            }
        }
    }
    return 0;
}

/*static int sAplayWait(const char* file) {
    pid_t pid = fork();
    if ( pid < 0) {
        return -1;
    }
    if ( pid == 0) {
        int rev = execl("/usr/bin/aplay", "/usr/bin/aplay", file ,(char*) NULL);
        if ( rev == -1 ) {
            printf("fork error:%s\n", strerror(errno));
        }
        _exit(1);
     } else {
        int status ;
        waitpid(pid, &status, 0);
    }

    return pid;
}*/



static int sProcessCommand(char* msg, int len) {
    int filedes[2];
    if (pipe(filedes) == -1) {
      printf("pipe error");
      return 1;
    }

    pid_t pid = fork();
    if ( pid < 0) {
        return -1;
    }
    int status ;
    if ( pid == 0) {
        while ((dup2(filedes[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
        close(filedes[1]);
        close(filedes[0]);
        int rev = execl(cmdproc, "processcmd.sh", "/dev/shm/test.wav", "/dev/shm/test.flac", lang, samprateStr, (char*) NULL);
        if ( rev == -1 ) {
            printf("fork error:%s\n", strerror(errno));
        }
        _exit(1);
    } else {
        waitpid(pid, &status, 0);
    }
    close(filedes[1]);
    if ( status == 0 && msg && len > 0) {
        ssize_t outlen = read(filedes[0], msg, len - 1);
        if ( outlen > 0 ) {
            msg[outlen] = 0;
        }
    }
    close(filedes[0]);
    return status;
}

/*static int isBye(const char* msg) {
    static char byebye[] = {0xe6, 0x8b, 0x9c, 0xe6, 0x8b, 0x9c, 0};
    static char byebye2[] = {0xe6, 0x8e, 0xb0, 0xe6, 0x8e, 0xb0, 0};
    if( msg ) {
        char* index = NULL;
        if ( (index = strstr( msg, byebye)) != NULL ) {
            return 1;
        }
        if ( (index = strstr( msg, byebye2)) != NULL ) {
            return 1;
        }
    }
    return 0;
}*/

static void
recognize_from_microphone()
{
    ad_rec_t *ad;
    int16 adbuf[2048];
    size_t adbuflen = 2048;
    uint8 in_speech; //utt_started, in_speech;
    int32 k;
    char const *hyp;
    state = kReady;
    int waitCount = 0;
    int humixCount = 0;
    char aplayCmd[1024];
    int nodeFD = sConnect2Node("/tmp/humix-speech-socket");
    int samprate = (int) cmd_ln_float32_r(config, "-samprate");

    WavWriter *wavWriter = NULL;
    WavPlayer player(1, samprate);
    if ( nodeFD == -1 ) {
        printf("Failed to open connect to node\n");
    }

    if ((ad = ad_open_dev(cmd_ln_str_r(config, "-adcdev"), samprate )) == NULL) {
        E_FATAL("Failed to open audio device\n");
    }
    if (ad_start_rec(ad) < 0) {
        E_FATAL("Failed to start recording\n");
    }

    if (ps_start_utt(ps) < 0) {
        E_FATAL("Failed to start utterance\n");
    }
    //utt_started = FALSE;
    printf("READY....\n");

    for (;;) {
        //read data from rec, we need to check if there is any aplay request from node first
        int needAplay = sGetAplayCommand(nodeFD, aplayCmd, 1024);
        if ( needAplay ) {
            ad_stop_rec(ad);
            printf("receive aplay command:%s\n", aplayCmd);
            player.play(aplayCmd);
            ad_start_rec(ad);
        }

        if ((k = ad_read(ad, adbuf, adbuflen)) < 0)
            E_FATAL("Failed to read audio\n");

        //start to process the data we got from rec
        ps_process_raw(ps, adbuf, k, FALSE, FALSE);
        in_speech = ps_get_in_speech(ps);
        
        switch (state) {
        case kReady:
            humixCount = 0;
            if ( in_speech ) {
                state = kKeyword;
                printf("Waiting for keyward: HUMIX... \n");
            }
            break;
        case kKeyword:
            if ( in_speech ) {
                //keep receiving keyword
            } else {
                //keyword done
                ps_end_utt(ps);
                hyp = ps_get_hyp(ps, NULL );
                if (hyp != NULL && strcmp("HUMIX", hyp) == 0) {
                    state = kWaitCommand;
                    printf("keyword HUMIX found\n");
                    fflush(stdout);
                    ad_stop_rec(ad);
                    player.play(wav_say);
                    ad_start_rec(ad);
                    printf("Waiting for a command...");
                    humixCount = 0;
                } else {
                    state = kReady;
                    printf("READY....\n");
                }
                if (ps_start_utt(ps) < 0)
                    E_FATAL("Failed to start utterance\n");
            }
            break;
        case kWaitCommand:
            if ( in_speech ) {
                printf("Listening the command...\n");
                state = kCommand;
                wavWriter = new WavWriter("/dev/shm/test.wav", 1, samprate);
                wavWriter->writeHeader();
                wavWriter->writeData((char*)adbuf, (size_t)(k*2));
            } else {
                //increase waiting count;
                if (++waitCount > 100) {
                    waitCount = 0;
                    if ( ++humixCount > 20 ) {
                        //exit humix-loop
                        state = kReady;
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
            if ( in_speech ) {
                //keep receiving command
                wavWriter->writeData((char*)adbuf, (size_t)(k*2));
            } else {
                wavWriter->writeData((char*)adbuf, (size_t)(k*2));
                delete wavWriter;
                wavWriter = NULL;
                //start to process command
                ps_end_utt(ps);
                printf("StT processing\n");
                char msg[1024];
                ad_stop_rec(ad);
                player.play(wav_proc);
                int result = sProcessCommand(msg, 1024);
                if ( result == 0 ) {
                    printf("%s", msg);
                } else {
                    printf("No command found!");
                }
                //once we got command, reset humix-loop
                humixCount = 0;
                ad_start_rec(ad);
                state = kWaitCommand;
                printf("Waiting for a command...\n");
                if (ps_start_utt(ps) < 0)
                    E_FATAL("Failed to start utterance\n");
            }
            break;
        }

        sleep_msec(20);
    }
    ad_close(ad);
}

int
main(int argc, char *argv[])
{
    char const *cfg;

    config = cmd_ln_parse_r(NULL, cont_args_def, argc, argv, TRUE);

    /* Handle argument file as -argfile. */
    if (config && (cfg = cmd_ln_str_r(config, "-argfile")) != NULL) {
        config = cmd_ln_parse_file_r(config, cont_args_def, cfg, FALSE);
    }

    if (config == NULL || (cmd_ln_str_r(config, "-cmdproc") == NULL)) {
    E_INFO("Specify '-cmdproc to processing the speech file");
    cmd_ln_free_r(config);
    return 1;
    }

    ps_default_search_args(config);
    ps = ps_init(config);
    if (ps == NULL) {
        cmd_ln_free_r(config);
        return 1;
    }

    E_INFO("%s COMPILED ON: %s, AT: %s\n\n", argv[0], __DATE__, __TIME__);
    //get processcmd.sh from arg
    cmdproc = cmd_ln_str_r(config, "-cmdproc");
    //get 'please say' wave file
    wav_say = cmd_ln_str_r(config, "-wav-say");
    //get 'processing' wave file
    wav_proc = cmd_ln_str_r(config, "-wav-proc");
    //get 'goodbye' wave file
    wav_bye = cmd_ln_str_r(config, "-wav-bye");
    //get language from arg
    lang = cmd_ln_str_r(config, "-lang");
    //get sample rate as string
    
    int samprate = (int) cmd_ln_float32_r(config, "-samprate");
    sprintf(samprateStr, "%d", samprate);

    //disable stdout buffer
    setbuf(stdout, NULL);
    recognize_from_microphone();

    ps_free(ps);
    cmd_ln_free_r(config);

    return 0;
}
