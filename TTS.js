var console = require('console');
var config = require('./config');
var sys = require('sys');
var nats = require('nats').connect();
var exec = require('child_process').exec;
var execSync = require('child_process').execSync;
var soap = require('soap');
var crypto = require('crypto');

var voice_path = "./controls/humix-sense-speech/voice/";
var url = 'http://tts.itri.org.tw/TTSService/Soap_1_3.php?wsdl';


function puts(error, stdout, stderr) {sys.puts(stdout)}

function convertText(text, hash, callback) {
    var args = {
        accountID: 'richchou',
        password: 'zaq12wsx',
        TTStext: text,
        TTSSpeaker: 'Bruce',
        volume: 50,
        speed: 0,
        outType: 'wav'
    };
    soap.createClient(url, function(err, client) {
        client.ConvertText(args, function(err, result) {
            if (err) {
                console.log('err: '+err);
                callback(err, null);
            }
            var id = result.Result.$value.split('&')[2];
            if (id) {
                console.log('get id: '+id);
                callback(null, id, hash);
            } else {
                var error = 'failed to convert text!';
                console.log(error);
                callback(error, null);
            }
        });
    });
}

function getConvertStatus(id, callback) {
    var args = {
        accountID: 'richchou',
        password: 'zaq12wsx',
        convertID: id
    };
    soap.createClient(url, function(err, client) {
        console.log("msg_id " + id);
        client.GetConvertStatus(args, function(err, result) {
            if (err) {
                console.log('err: '+err);
                callback(err, null);
            }
            var downloadUrl = result.Result.$value.split('&')[4];
            if (downloadUrl) {
                //console.log('get download url: '+downloadUrl);
                console.log(id + " " + downloadUrl);
                var wav_file = voice_path + "text/" + id + ".wav";
                execSync("wget "+ downloadUrl + " -O " + wav_file, null);
                callback(null, id);
            } else {
                var error = 'Still converting! result: '+JSON.stringify(result);
                console.log(error);
                callback(error, null);
            }
        });
    });
}

var retry = 0;
function download (id) {
    retry++;
    console.log(id+ " " +" download" );
    getConvertStatus(id, function(err, result) {
        if (err) 
        { 
            console.log('err: '+err); 
            if (retry < 10)
            {
               console.log("retry " + retry);
               setTimeout(download, 2000, id);
            }
        }
        else 
        {
           var wav_file = voice_path + "text/" + result + ".wav";
           console.log('Play wav file: ' + wav_file);
           execSync("aplay "+ wav_file, null);
        }
    });
}


var msg = '';
var wavehash = new Object();
// subscribe events
nats.subscribe('humix.sense.speech.command', function(msg) {
    console.log('Received a message: ' + msg);
    var text = JSON.parse(msg).text || undefined,
        wav_file = '';

    if (!text) {
        return console.error('Missing property: msg.text');
    }

    var hash = crypto.createHash('md5').update(text).digest('hex');
    console.log ("hash value: " + hash);
    if (wavehash.hasOwnProperty(hash)) {
        var wav_file = voice_path + "text/" + wavehash[hash] + ".wav";
        console.log('Play hash wav file: ' +  wav_file);
        execSync("aplay "+ wav_file, null);
    }
    else {
        console.log("hash not found");
        convertText(text, hash, function(err, id, hashvalue) {
            if (err) { console.log(err); }
            else {
                wavehash[hashvalue] = id;
                retry = 0;
                setTimeout(download, 1000, id);
            }
        });
    }
});

//use child process to handle speech to text
var speechProc = exec(config.speechCmd + ' ' + config.args.join(' '), function () {
});

var commandRE = /---=(.*)=---/;
var prefix = '---="';

speechProc.stdout.on('data', function (data) {
    var data = data.trim();
    if ( commandRE.test(data) ) {
        nats.publish('humix.sense.speech.event', data.substr(prefix.length, data.length- (prefix.length * 2)));
        console.error('command found:' + data.substr(prefix.length, data.length - (prefix.length * 2)));
    }
});

speechProc.on('close', function(code) {
    console.error('speech proc finished with code:' + code);
});

speechProc.on('error', function (error) {
    console.error(error);
});
