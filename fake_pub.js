var nats = require('nats').connect();
var speech_command = { "sensor" : "temp",
                       "value" : 20 }

// publish events
//nats.publish("humix.sense.speech.command","hello jeffrey");
//nats.publish("humix.sense.speech.command","age/10");
nats.publish("humix.sense.speech.command",JSON.stringify(speech_command));


var speech_command = { "sensor" : "age",
                       "value" : 38 }
nats.publish("humix.sense.speech.command",JSON.stringify(speech_command));

