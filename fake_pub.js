var nats = require('nats').connect();

// publish events
nats.publish("humix.sense.speech.command","hello jeffrey");

