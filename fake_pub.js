var nats = require('nats').connect();

// publish events
/*
var speech_command = { "sensor" : "temp",
                       "value" : 20 }
nats.publish("humix.sense.speech.command",JSON.stringify(speech_command));

var speech_command = { "sensor" : "age",
                       "value" : 38 }
nats.publish("humix.sense.speech.command",JSON.stringify(speech_command));
*/
var speech_command = { "text" : "這不是一個測試" };
nats.publish("humix.sense.speech.command",JSON.stringify(speech_command));
