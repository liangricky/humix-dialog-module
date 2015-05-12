var console = require('console');
var sys = require('sys');
var nats = require('nats').connect();
var exec = require('child_process').exec;
var voice_path = "./voice/";
function puts(error, stdout, stderr) {sys.puts(stdout)}

var msg = '';
// subscribe events
nats.subscribe('humix.sense.speech.command', function(msg) {
   speech_command = JSON.parse(msg);
   var wav_file = voice_path + speech_command.sensor + "/" + speech_command.value + ".wav";
   exec("aplay "+ wav_file, null);
   console.log('Received a message: ' + msg);
   console.log('Play wav file: ' + wav_file);
});
