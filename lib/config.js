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
        'lm'            : './lm/humix.lm',
        'dict'          : './lm/humix.dic',
        'samprate'      : '16000',
        'cmdproc'       : './util/processcmd.sh',
        'lang'          : 'zh-tw',
        'wav-proc'      : './voice/interlude/beep2.wav',
        'wav-bye'       : './voice/interlude/beep1.wav',
        'logfn'         : '/dev/null'},
    'engine' : 'google', // 'google' or 'watson'
    'watson' : { 'username' :'8765e402-1084-4730-be92-104f2cde72e4',
        'passwd' : 'NsSQsB6QqHJ7'},
    'google' : { 'username' : 'xxxxx',
        'passwd': 'AIzaSyBOti4mM-6x9WDnZIjIeyEU21OpBXqWBgw'}
};
