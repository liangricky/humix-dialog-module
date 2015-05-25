#!/bin/sh
pocketsphinx_continuous -vad_threshold 3.5 -upperf 1000 -hmm /usr/local/share/pocketsphinx/model/en-us/en-us -lm /home/pi/humix/sphinx/knowledge-base/humix.lm -dict /home/pi/humix/sphinx/knowledge-base/humix.dic -samprate 16000 -inmic yes
