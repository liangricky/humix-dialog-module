/*******************************************************************************
* Copyright (c) 2015 IBM Corp.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/
/*eslint-env node */
'use strict';
var console = require('console');
var config  = require('./lib/config');
var sys     = require('util');
var nats    = require('nats').connect();
var exec    = require('child_process').exec;
var execSync = require('child_process').execSync;
var soap    = require('soap');
var crypto  = require('crypto');
var net     = require('net');
var fs      = require('fs');
var Buffer  = require('buffer').Buffer;
var path    = require('path');
var watson  = require('watson-developer-cloud');
//var Sound   = require('node-aplay'); 
var HumixSense = require('node-humix-sense');
var HumixSpeech = require('./lib/HumixSpeech').HumixSpeech;

var voice_path = path.join(__dirname, 'voice');
var url = 'http://tts.itri.org.tw/TTSService/Soap_1_3.php?wsdl';
var kGoogle = 0,
    kWatson = 1;
var engineIndex = {'google': kGoogle, 'watson': kWatson };

var ttsWatson;
var retry = 0;



if (config['tts-engine'] === 'watson') {
    ttsWatson = watson.text_to_speech({
        'username': config.tts.watson.username,
        'password': config.tts.watson.passwd,
        version: 'v1',
    });
}

var moduleConfig = {
    "moduleName":"humix-dialog",
    "commands" : ["say"],
    "events" : ["speech"],
    "debug": true
}

var humix = new HumixSense(moduleConfig);
var hsm;

humix.on('connection', function(humixSensorModule){

    hsm = humixSensorModule;

    console.log('Communication with humix-sense is now ready.');

    hsm.on('say', function(data){
        console.log('data:'+data);
        text2Speech(data);
    });  // end of say command
});


/* 
 * Speech To Text Processing
 */

//start HumixSpeech here
var hs;
var commandRE = /---="(.*)"=---/;

/**
 * callback function that is called when
 * HumixSpeech detect a valid command/sentence
 * @param cmdstr a command/sentence in this format:
 *         '----="command string"=---'
 */
function receiveCommand(cmdstr) {
    cmdstr = cmdstr.trim();
    if ( config.stt.engine ) {
        console.error('command found:', cmdstr);
        
        if(hsm)
            hsm.event("speech", cmdstr);

    } else {
        console.log(" No stt engine configured. Skip");
    }
}


try {
    hs = new HumixSpeech(config.options);
    var engine = config['stt-engine'] || 'google';
    hs.engine( config.stt[engine].username, config.stt[engine].passwd,
    		engineIndex[engine], require('./lib/' + engine).startSession);
    hs.start(receiveCommand);
} catch ( error ) {
    console.error(error);
}


/* 
 * Text To Speech Processing
 */

function text2Speech(msg) {
    console.log('Received a message:', msg);
    var text
    var wav_file = '';
    try {
        text = JSON.parse(msg).text;
    } catch (e) {
        console.error('invalid JSON format. Skip');
        return;
    }

    if (!text) {
        return console.error('Missing property: msg.text');
    }

    //for safe
    text = text.trim();

    var hash = crypto.createHash('md5').update(text).digest('hex');
    var filename = path.join(voice_path, 'text', hash + '.wav');
         
    console.log ('Check filename:', filename);
   
    if(fs.existsSync(filename)){
        
        console.log('Wav file exist. Play cached file:', filename);
        sendAplay2HumixSpeech(filename);
    } else {

        console.log('Wav file does not exist');
      
        var ttsEngine = config['tts-engine'];

        console.log('tts-engine:', engine);            
        if ( ttsEngine === 'itri') {

            ItriTTS(msg, function(err, id) {
                if (err) {

                    console.log('failed to download wav from ITRI. Error:' + err);

                } else {
                  
                    retry = 0;
                    setTimeout(ItriDownload, 1000, id, filename);
                }
            });

        }else if ( ttsEngine === 'watson') {

            WatsonTTS(msg, filename);             
        }
      
    }
   
}



/**
 * call the underlying HumixSpeech to play wave file
 * @param file wave file
 */
function sendAplay2HumixSpeech( file ) {
    if( hs ) {
        hs.play(file);
    }
}


/* 
 * Watson TTS Processing
 */

function WatsonTTS(msg,filename) {

      ttsWatson.synthesize({ text : msg , 'accept': 'audio/wav'}, function() {
     
          console.log("wav_path:",en_wav_file)          
          fs.writeFileSync( filename, new Buffer(arguments[1]));
          sendAplay2HumixSpeech(filename);
      });
}



/* 
 * ITRI TTS Processing
 */

function ItriTTS(text, callback) {
    var args = {
        accountID: config.tts.itri.username,
        password: config.tts.itri.passwd,
        TTStext: text,
        TTSSpeaker: config.tts.itri.speaker,
        volume: 50,
        speed: -2,
        outType: 'wav'
    };
    soap.createClient(url, function(err, client) {
        client.ConvertText(args, function(err, result) {
            if (err) {
                console.log('err:', err);
                callback(err, null);
            }
            try {
                var id = result.Result.$value.split('&')[2];
                if (id) {
                    console.log('get id:', id);
                    callback(null, id);
                } else {
                    throw 'failed to convert text!';
                }
            } catch (e) {
                console.log(error);
                callback(error, null);
            }
        });
    });
}

function ItriGetConvertStatus(id, filename, callback) {
    var args = {
        accountIDhasOwnProperty: config.tts.itri.username,
        password: config.tts.itri.passwd,
        convertID: id
    };
    soap.createClient(url, function(err, client) {
        console.log('msg_id', id);
        client.GetConvertStatus(args, function(err, result) {
            if (err) {
                console.log('err:', err);
                callback(err, null);
            }
            var downloadUrl = result.Result.$value.split('&')[4];
            if (downloadUrl) {
               
                console.log(id, downloadUrl);
                execSync('wget '+ downloadUrl + ' -O ' + filename, {stdio: [ 'ignore', 'ignore', 'ignore' ]});
                callback(null, filename);
            } else {
                var error = 'Still converting! result: '+JSON.stringify(result);
                console.log(error);
                callback(error, null);
            }
        });
    });
}


function ItriDownload (id, filename) {
    retry++;
    console.log(id, 'download' );
    ItriGetConvertStatus(id, filename, function(err, filename) {
        if (err) 
        { 
            console.log('err:', err); 
            if (retry < 10)
            {
               console.log('retry', retry);
               setTimeout(ItriDownload, 2000, id, filename);
            }
        }
        else 
        {
           console.log('Play wav file:', filename);
           sendAplay2HumixSpeech(filename);
        }
    });
}



/* 
 * Signal Handling
 */

process.stdin.resume();
function cleanup() {
    if (hs) {
        hs.stop();
    }
}
process.on('SIGINT', function() {
    cleanup();
    process.exit(0);
});
process.on('SIGHUP', function() {
    cleanup();
    process.exit(0);
});
process.on('SIGTERM', function() {
    cleanup();
    process.exit(0);
});
process.on('exit', function() {
    cleanup();
});
process.on('error', function() {
    cleanup();
});

process.on('uncaughtException', function(err) {
    if ( err.toString().indexOf('connect ECONNREFUSED') ) {
        console.error('exception,', JSON.stringify(err));
        //cleanup();
        //process.exit(0);
    }
});

