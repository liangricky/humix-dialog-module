var watson = require('watson-developer-cloud');

exports.startSession = function (username, passwd, callback) {
    var speech_to_text = watson.speech_to_text({
        'username': username,
        'password': passwd,
        version: 'v1',
        url: 'https://stream.watsonplatform.net/speech-to-text/api'
    });

    var rev = speech_to_text.createRecognizeStream(
            {   'content_type': 'audio/l16;rate=16000',
                'interim_results': true,
                'continuous': true,
                'inactivity_timeout': -1,
                'model': 'zh-CN_BroadbandModel'});

    rev.on('results', function (data) {
        var index = data.results.length ? data.results.length - 1 : 0;
        if(data.results[index] && data.results[index].final && data.results[index].alternatives && callback) {
            callback(data.results[index].alternatives[0].transcript);
        }
    });

    rev.on('connection-close', function (code, description) {
        console.error('Watson STT WS connection-closed,', code, description);
    });

    rev.on('connect', function (conn) {
        console.info('Watson STT WS connected');
    });
    return rev;
}
