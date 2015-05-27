#!/bin/sh
pocketsphinx_continuous -vad_threshold 4 -upperf 1000 -hmm /usr/local/share/pocketsphinx/model/en-us/en-us -lm /home/pi/humix/humix-sense/controls/humix-sense-speech/humix.lm -dict /home/pi/humix/humix-sense/controls/humix-sense-speech/humix.dic -samprate 16000 -inmic yes
