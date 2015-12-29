module.exports = {
    'options' : { 
        'vad_threshold' : '3.5', 
        'upperf'        : '1000',
        'hmm'           : './deps/pocketsphinx-5prealpha/model/en-us/en-us',
        'lm'            : './humix.lm',
        'dict'          : './humix.dic',
        'samprate'      : '16000',
        'cmdproc'       : './processcmd.sh',
        'lang'          : 'zh-tw',
        'logfn'         : '/dev/null'},
    'responses': ['voice/interlude/what.wav'],
    'repeats': ['voice/interlude/repeat1.wav', 'voice/interlude/repeat2.wav']
};
