var console = require('console');
var sys = require('sys');
var nats = require('nats').connect();
var exec = require('child_process').exec;
var execSync = require('child_process').execSync;
var soap = require('soap');

var voice_path = "./voice/";
var url = 'http://tts.itri.org.tw/TTSService/Soap_1_3.php?wsdl';


function puts(error, stdout, stderr) {sys.puts(stdout)}

function convertText(text, callback) {
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
                callback(null, id);
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

function download (id) {

    console.log(id+ " " +" download" );
    getConvertStatus(id, function(err, result) {
        if (err) { console.log('err: '+err); }
           var wav_file = voice_path + "text/" + result + ".wav";
           console.log('Play wav file: ' + wav_file);
           execSync("aplay "+ wav_file, null);
    });
}


var msg = '';
// subscribe events
nats.subscribe('humix.sense.speech.command', function(msg) {
   console.log('Received a message: ' + msg);
   speech_command = JSON.parse(msg);
   var wav_file = '';
   switch (speech_command.sensor)
   {
       case "temp":
       case "humid":
       case "age":
           wav_file = voice_path + speech_command.sensor + "/" + speech_command.value + ".wav";
           exec("aplay "+ wav_file, null);
           console.log('Play wav file: ' + wav_file);
           break;
       case "text":
           convertText(speech_command.value, function(err, id) {
               if (err) { console.log(err); }
               setTimeout(download, 7000, id);
            });
           break;
       
   }
  /* 
   if (speech_command.sensor == "temp" ||
       speech_command.sensor == "humid" ||
       speech_command.sensor == "age" )
   {
   }
  */ 
});
