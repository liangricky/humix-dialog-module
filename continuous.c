/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ====================================================================
 * Copyright (c) 1999-2010 Carnegie Mellon University.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced 
 * Research Projects Agency and the National Science Foundation of the 
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*
 * continuous.c - Simple pocketsphinx command-line application to test
 *                both continuous listening/silence filtering from microphone
 *                and continuous file transcription.
 */

/*
 * This is a simple example of pocketsphinx application that uses continuous listening
 * with silence filtering to automatically segment a continuous stream of audio input
 * into utterances that are then decoded.
 * 
 * Remarks:
 *   - Each utterance is ended when a silence segment of at least 1 sec is recognized.
 *   - Single-threaded implementation for portability.
 *   - Uses audio library; can be replaced with an equivalent custom library.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#else
#include <sys/select.h>
#endif

#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>

#include "pocketsphinx.h"

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
    {"-infile",
     ARG_STRING,
     NULL,
     "Audio file to transcribe."},
    {"-inmic",
     ARG_BOOLEAN,
     "no",
     "Transcribe audio from microphone."},
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
static FILE *rawfd;
static char const* cmdproc = NULL;
static char const* wav_say = NULL;
static char const* wav_proc = NULL;
static char const* wav_bye = NULL;
static char const* lang = NULL;

static void
print_word_times()
{
    int frame_rate = cmd_ln_int32_r(config, "-frate");
    ps_seg_t *iter = ps_seg_iter(ps, NULL);
    while (iter != NULL) {
        int32 sf, ef, pprob;
        float conf;

        ps_seg_frames(iter, &sf, &ef);
        pprob = ps_seg_prob(iter, NULL, NULL, NULL);
        conf = logmath_exp(ps_get_logmath(ps), pprob);
        printf("%s %.3f %.3f %f\n", ps_seg_word(iter), ((float)sf / frame_rate),
               ((float) ef / frame_rate), conf);
        iter = ps_seg_next(iter);
    }
}

static int
check_wav_header(char *header, int expected_sr)
{
    int sr;

    if (header[34] != 0x10) {
        E_ERROR("Input audio file has [%d] bits per sample instead of 16\n", header[34]);
        return 0;
    }
    if (header[20] != 0x1) {
        E_ERROR("Input audio file has compression [%d] and not required PCM\n", header[20]);
        return 0;
    }
    if (header[22] != 0x1) {
        E_ERROR("Input audio file has [%d] channels, expected single channel mono\n", header[22]);
        return 0;
    }
    sr = ((header[24] & 0xFF) | ((header[25] & 0xFF) << 8) | ((header[26] & 0xFF) << 16) | ((header[27] & 0xFF) << 24));
    if (sr != expected_sr) {
        E_ERROR("Input audio file has sample rate [%d], but decoder expects [%d]\n", sr, expected_sr);
        return 0;
    }
    return 1;
}

/*
 * Continuous recognition from a file
 */
static void
recognize_from_file()
{
    int16 adbuf[4096*2];
    const char *fname;
    const char *hyp;
    int32 k;
    uint8 utt_started, in_speech;
    int32 print_times = cmd_ln_boolean_r(config, "-time");

    fname = cmd_ln_str_r(config, "-infile");
    if ((rawfd = fopen(fname, "rb")) == NULL) {
        E_FATAL_SYSTEM("Failed to open file '%s' for reading",
                       fname);
    }
    
    if (strlen(fname) > 4 && strcmp(fname + strlen(fname) - 4, ".wav") == 0) {
        char waveheader[44];
	fread(waveheader, 1, 44, rawfd);
	if (!check_wav_header(waveheader, (int)cmd_ln_float32_r(config, "-samprate")))
    	    E_FATAL("Failed to process file '%s' due to format mismatch.\n", fname);
    }
    
    ps_start_utt(ps);
    utt_started = FALSE;

    while ((k = fread(adbuf, sizeof(int16), 4096*2, rawfd)) > 0) {
        ps_process_raw(ps, adbuf, k, FALSE, FALSE);
        in_speech = ps_get_in_speech(ps);
        if (in_speech && !utt_started) {
            utt_started = TRUE;
        } 
        if (!in_speech && utt_started) {
            ps_end_utt(ps);
            hyp = ps_get_hyp(ps, NULL);
            if (hyp != NULL)
        	printf("%s\n", hyp);
            if (print_times)
        	print_word_times();

            ps_start_utt(ps);
            utt_started = FALSE;
        }
    }
    ps_end_utt(ps);
    if (utt_started) {
        hyp = ps_get_hyp(ps, NULL);
        if (hyp != NULL)
    	    printf("%s\n", hyp);
        if (print_times) {
        print_word_times();
	}
    }
    
    fclose(rawfd);
}

/* Sleep for specified msec */
static void
sleep_msec(int32 ms)
{
#if (defined(_WIN32) && !defined(GNUWINCE)) || defined(_WIN32_WCE)
    Sleep(ms);
#else
    /* ------------------- Unix ------------------ */
    struct timeval tmo;

    tmo.tv_sec = 0;
    tmo.tv_usec = ms * 1000;

    select(0, NULL, NULL, NULL, &tmo);
#endif
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

static int sAplay(const char* file) {
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
}

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
                        printf(msg);
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

    if (config == NULL || (cmd_ln_str_r(config, "-infile") == NULL && cmd_ln_boolean_r(config, "-inmic") == FALSE)) {
    E_INFO("Specify '-infile <file.wav>' to recognize from file or '-inmic yes' to recognize from microphone.");
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
    if (cmd_ln_str_r(config, "-infile") != NULL) {
        recognize_from_file();
    } else if (cmd_ln_boolean_r(config, "-inmic")) {
        recognize_from_microphone();
    }

    ps_free(ps);
    cmd_ln_free_r(config);

    return 0;
}

#if defined(_WIN32_WCE)
#pragma comment(linker,"/entry:mainWCRTStartup")
#include <windows.h>
//Windows Mobile has the Unicode main only
int
wmain(int32 argc, wchar_t * wargv[])
{
    char **argv;
    size_t wlen;
    size_t len;
    int i;

    argv = malloc(argc * sizeof(char *));
    for (i = 0; i < argc; i++) {
        wlen = lstrlenW(wargv[i]);
        len = wcstombs(NULL, wargv[i], wlen);
        argv[i] = malloc(len + 1);
        wcstombs(argv[i], wargv[i], wlen);
    }

    //assuming ASCII parameters
    return main(argc, argv);
}
#endif
