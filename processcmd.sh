#!/bin/sh

filename=$1
outfile=$2
lang=$3

RESULT=$(flac $filename -f --best --sample-rate 16000 -o $outfile 1>/dev/shm/voice.log 2>/dev/shm/voice.log; curl -X POST --data-binary @$outfile --user-agent 'Mozilla/5.0' --header 'Content-Type: audio/x-flac; rate=16000;' "https://www.google.com/speech-api/v2/recognize?output=json&lang=$lang&key=AIzaSyBOti4mM-6x9WDnZIjIeyEU21OpBXqWBgw&client=Mozilla/5.0" | sed -e 's/[{}]/''/g' | awk -F":" '{print $4}' | awk -F"," '{printf "---=%s=---", $1}' | sed s/---==---//g)
if [ $RESULT ]; then
    echo $RESULT;
    exit 0;
else
    exit 1;
fi
