#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include <sys/select.h>
#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <pocketsphinx.h>

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
     "/home/pi/humix/humix-sense/controls/humix-sense-speech/processcmd.sh",
     "command processor."},
    {"-wav-say",
     ARG_STRING,
     "/home/pi/humix/humix-sense/controls/humix-sense-speech/voice/interlude/pleasesay.wav",
     "the wave file of saying."},
    {"-wav-proc",
     ARG_STRING,
     "/home/pi/humix/humix-sense/controls/humix-sense-speech/voice/interlude/process.wav",
     "the wave file of processing."},
    {"-wav-bye",
     ARG_STRING,
     "/home/pi/humix/humix-sense/controls/humix-sense-speech/voice/interlude/bye.wav",
     "the wave file of goodbye."},
    {"-lang",
     ARG_STRING,
     "zh-tw",
     "language locale."},

    CMDLN_EMPTY_OPTION
};

static ps_decoder_t *ps;
static cmd_ln_t *config;
static char const* cmdproc = NULL;
static char const* wav_say = NULL;
static char const* wav_proc = NULL;
static char const* wav_bye = NULL;
static char const* lang = NULL;

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

/*
 * Main utterance processing loop:
 *     for (;;) {
 *        start utterance and wait for speech to process
 *        decoding till end-of-utterance silence will be detected
 *        print utterance result;
 *     }
 */


typedef enum {
    kReady,
    kKeyword,
    kWaitCommand,
    kCommand
} State;

static State state;

static int sStartRecord() {
    pid_t pid = fork();
    if ( pid < 0) {
        return -1;
    }
    if ( pid == 0) {
        int rev = execl("/usr/bin/arecord", "/usr/bin/arecord", "-f", "cd", "-r", "16000", "-t", "wav", "/dev/shm/test.wav", (char*) NULL);
        if ( rev == -1 ) {
            printf("fork error:%s\n", strerror(errno));
        }
        _exit(1);
    }
    return pid;
}

/*static int sAplay(const char* file) {
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
    }
    return pid;
}*/

static int sAplayWait(const char* file) {
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
}



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
        int rev = execl(cmdproc, "processcmd.sh", "/dev/shm/test.wav", "/dev/shm/test.flac", lang, (char*) NULL);
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

static int isBye(const char* msg) {
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
}

static void
recognize_from_microphone()
{
    ad_rec_t *ad;
    int16 adbuf[4096];
    uint8 in_speech; //utt_started, in_speech;
    int32 k;
    char const *hyp;
    state = kReady;
    int waitCount = 0;
    pid_t recordPID = 0;
    int humixCount = 0;


    if ((ad = ad_open_dev(cmd_ln_str_r(config, "-adcdev"),
                          (int) cmd_ln_float32_r(config,
                                                 "-samprate"))) == NULL)
        E_FATAL("Failed to open audio device\n");
    if (ad_start_rec(ad) < 0)
        E_FATAL("Failed to start recording\n");

    if (ps_start_utt(ps) < 0)
        E_FATAL("Failed to start utterance\n");
    //utt_started = FALSE;
    printf("READY....\n");

    for (;;) {
        if ((k = ad_read(ad, adbuf, 4096)) < 0)
            E_FATAL("Failed to read audio\n");
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
                    sAplayWait(wav_say);
                    ad_start_rec(ad);
                    printf("Waiting for a command...\n");
                    humixCount = 0;
                    //also start recording
                    recordPID = sStartRecord();
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
            } else {
                //increase waiting count;
                if (++waitCount > 60) {
                    waitCount = 0;
                    if ( ++humixCount > 20 ) {
                        //exit humix-loop
                        int pids = 0;
                        kill(recordPID, SIGTERM);
                        waitpid(recordPID, &pids, 0);
                        recordPID = 0;
                        state = kReady;
                        printf("READY....\n");
                   } else {
                        //still in humix-loop but we need to restart to recording
                        //in order to avoid the file becomes too large
                        //stop the recording as well
                        int pids = 0;
                        kill(recordPID, SIGTERM);
                        waitpid(recordPID, &pids, 0);
                        recordPID = 0;
                        recordPID = sStartRecord();
                        printf("Waiting for a command...\n");
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
            } else {
                //start to process command
                int pids = 0;
                kill(recordPID, SIGTERM);
                waitpid(recordPID, &pids, 0);
                recordPID = 0;
                char msg[1024];
                int result = sProcessCommand(msg, 1024);
                int bye = 0;
                if ( result == 0 ) {
                    bye = isBye(msg);
                    if ( !bye ) {
                        //output the command
                        printf("%s", msg);
                        ad_stop_rec(ad);
                        sAplayWait(wav_proc);
                        ad_start_rec(ad);
                    }
                }
                //once we got command, reset humix-loop
                humixCount = 0;
                if ( bye ) {
                    ad_stop_rec(ad);
                    sAplayWait(wav_bye);
                    ad_start_rec(ad);
                    state = kReady;
                    printf("READY....\n");
                } else {
                    //also restart recording
                    recordPID = sStartRecord();
                    state = kWaitCommand;
                    printf("Waiting for a command...\n");
                }
                ps_end_utt(ps);
                if (ps_start_utt(ps) < 0)
                    E_FATAL("Failed to start utterance\n");
            }
            break;
        }

        sleep_msec(100);
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

    //disable stdout buffer
    setbuf(stdout, NULL);
    recognize_from_microphone();

    ps_free(ps);
    cmd_ln_free_r(config);

    return 0;
}
