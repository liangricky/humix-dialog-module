var watson = require('watson-developer-cloud');

exports.startSession = function (username, passwd, callback) {
    var speech_to_text = watson.speech_to_text({
        'username': username,
        'password': passwd,
        version: 'v1',
        url: 'https://stream.watsonplatform.net/speech-to-text/api'
    });

    var rev = speech_to_text.createRecognizeStream(
            {   'content-type': 'audio/l16; rate=16000',
                'interim_results': true,
                'continuous': true});

    rev.on('results', function (data) {
        console.error('data:', JSON.stringify(data));
        if(data.results[0].final && data.results[0].alternatives) {
            if ( callback ) {
                callback(data.results[0].alternatives[0].transcript);
            }
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