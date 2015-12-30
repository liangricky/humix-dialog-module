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

module.exports = {
    'options' : { 
        'vad_threshold' : '3.5', 
        'upperf'        : '1000',
        'hmm'           : './deps/pocketsphinx-5prealpha/model/en-us/en-us',
        'lm'            : './humix.lm',
        'dict'          : './humix.dic',
        'samprate'      : '16000',
        'cmdproc'       : './processcmd.sh',
        'lang'          : 'zh-tw',
        'logfn'         : '/dev/null'},
    'responses': ['voice/interlude/what.wav'],
    'repeats': ['voice/interlude/repeat1.wav', 'voice/interlude/repeat2.wav']
};
