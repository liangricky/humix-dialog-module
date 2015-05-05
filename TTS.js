var console = require('console');
var sys = require('sys');
var nats = require('nats').connect();
var exec = require('child_process').exec;
function puts(error, stdout, stderr) {sys.puts(stdout)}

var msg = '';
// subscribe events
nats.subscribe('humix.sense.speech.command', function(msg) {
   exec("flite -t \""+ msg + "\"", puts);
   console.log('Received a message: ' + msg);
});
//exec("flite -t \"hello world\"", puts);
