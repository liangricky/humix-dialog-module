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
module.exports = {
    'options' : { 
        'vad_threshold' : '3.5', 
        'upperf'        : '1000',
        'hmm'           : './deps/pocketsphinx-5prealpha/model/en-us/en-us',
        //http://www.speech.cs.cmu.edu/tools/lmtool.html use this website
        //to generate lm and dict file based on your keyword-name
        'lm'            : './lm/humix.lm',
        'dict'          : './lm/humix.dic',
        'keyword-name'  : 'HUMIX', //all capital characters
        'samprate'      : '16000',
        'maxwpf'        : '5',
        'topn'          : '2',
        'maxhmmpf'      : '3000',
        'pl_window'     : '7',
        'ds'            : '2',
        'cmdproc'       : './util/processcmd.sh',
        'lang'          : 'zh-tw',
        'wav-proc'      : './voice/interlude/beep2.wav',
        'wav-bye'       : './voice/interlude/beep1.wav',
        'logfn'         : '/dev/null'},
    'stt-engine' : 'watson', // 'google' or 'watson',
    'tts-engine' : 'watson', //'watson' or 'itri'
    'stt' : {
      'watson' : { 'username' :'<replace as your appid',
          'passwd' : '<replace as your passed'},
      'google' : { 'username' : 'xxxxx',
          'passwd': '<replace as your appid'}
            },
    'tts' :{
      'watson' : { 'username' :'9520339d-2bac-4418-a130-33e17850afc8',
                   'passwd' : 'bk8kip3Ejt2k'},
      'itri' : { 'username' : 'richchou',
                 'passwd': 'zaq12wsx',
                 'speaker': 'Bruce',
                }
            }
};
