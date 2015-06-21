module.exports = {
    'speechCmd' : 'pocketsphinx_continuous',
    'args' : ['-vad_threshold', '3.5', '-upperf', '1000', '-hmm', '/usr/local/share/pocketsphinx/model/en-us/en-us', '-lm', '/home/liuch/workspace/humix/humix-sense/controls/humix-sense-speech/humix.lm', '-dict', '/home/liuch/workspace/humix/humix-sense/controls/humix-sense-speech/humix.dic', '-samprate', '16000', '-inmic', 'yes', '-cmdproc', '/home/liuch/workspace/humix/humix-sense/controls/humix-sense-speech/processcmd.sh', '-lang', 'zh-tw'],
    'responses': ['/home/liuch/workspace/humix/humix-sense/controls/humix-sense-speech/voice/interlude/what.wav'],
    'repeats': ['/home/liuch/workspace/humix/humix-sense/controls/humix-sense-speech/voice/interlude/repeat1.wav', '/home/liuch/workspace/humix/humix-sense/controls/humix-sense-speech/voice/interlude/repeat2.wav']
};
