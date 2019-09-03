# FreeSWITCH ASR模块

[阿里云ASR](https://help.aliyun.com/product/30413.html?spm=a2c4g.11186623.2.10.6b634c07NBBDiY)和FreeSWITCH直接对接，识别结果通过ESL输出  
阿里云语音识别SDK: [**NlsSdkCpp2.0**](http://download.taobaocdn.com/freedom/33762/compress/NlsSdkCpp2.zip?spm=a2c4g.11186623.2.15.35312318Ptx8fD&file=NlsSdkCpp2.zip)  

### 编译安装

1. 安装Freeswitch [**Install**](https://freeswitch.org/confluence/display/FREESWITCH/CentOS+7+and+RHEL+7)

2. 下载`mod_asr`代码
```
git clone https://github.com/telegant/mod_asr
```
3. 编译
FreeSWITCH和NlsSdkCpp2.0路径根据自己情况修改
```
make
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib/linux
```
4. 安装
```
cp mod_asr.so /usr/local/freeswitch/mod
#编辑modules.conf.xml添加mod_asr模块
vim /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml
<load module="mod_asr"/>
```
5. 验证
启动freeswitch查看mod_asr是否加载成功
```
freeswitch -nc -nonat
fs_cli -x "show modules"|grep asr
application,start_asr,mod_asr,/usr/local/freeswitch/mod/mod_asr.so
application,stop_asr,mod_asr,/usr/local/freeswitch/mod/mod_asr.so
```

#### 使用

1. 申请阿里云AccessKey和Secret
2. fs_cli执行

start_asr参数:
```
[start_asr:<your appkey> <your AccessKey ID> <your AccessKey Secret> <Asrleg> <iSilence> <sUrl>]
```
```
originate user/1001 'start_asr:un2wEHfu32KN5ELW Y90VLNxDfMOKWZBqsEJigUn1Wpilbf LTAIwloSbAnFxcWK bleg 300 null,park' inline
```
3. dialplan执行
```
<extension name="asr">
    <condition field="destination_number" expression="^.*$">
        <action application="answer"/>
        <action application="start_asr" data="un2wEHfu32KN5ELW Y90VLNxDfMOKWZBqsEJigUn1Wpilbf LTAIwloSbAnFxcWK aleg 300 null"/>
        <action application="park"/>
    </condition>
</extension>
```
#### 开发
订阅`CUSTOM asr_start` `CUSTOM asr_update` `CUSTOM asr_stop` 事件
fs_cli可以通过`/event Custom asr_start  asr_update asr_stop`订阅事件
识别结果通过esl输出
```
RECV EVENT
Event-Subclass: asr_start
Event-Name: CUSTOM
Core-UUID: 6882dc67-75c4-4f4f-b767-99c03281a6cc
FreeSWITCH-Hostname: snapshot
FreeSWITCH-Switchname: snapshot
FreeSWITCH-IPv4: 10.11.11.205
FreeSWITCH-IPv6: ::1
Event-Date-Local: 2019-08-19 18:27:17
Event-Date-GMT: Mon, 19 Aug 2019 10:27:17 GMT
Event-Date-Timestamp: 1566210437305129
Event-Calling-File: mod_asr.cpp
Event-Calling-Function: onSentenceEnd
Event-Calling-Line-Number: 278
Event-Sequence: 1357
UUID: e66e1cf8-6840-4a1e-a44f-173be182bf0f
ASR-Response: {"header":{"namespace":"SpeechTranscriber","name":"SentenceEnd","status":20000000,"message_id":"4214eaa99b0a4de98e7a92f66fb310f1","task_id":"fc56bab0a08f471c88f0dffe50de4111","status_text":"Gateway:SUCCESS:Success."},"payload":{"index":13,"time":79560,"begin_time":77550,"result":"行行行，好，嗯，好的好好好，拜拜，嗯。","confidence":0.0,"words":[],"status":20000000,"stash_result":{"sentenceId":0,"beginTime":0,"text":"","currentTime":0}}}
Channel: sofia/internal/1008@ehuu.com
Timestamp: 20190819182717305129
Answered: true
ASRLeg: bleg


RECV EVENT
Event-Subclass: asr_update
Event-Name: CUSTOM
Core-UUID: 6882dc67-75c4-4f4f-b767-99c03281a6cc
FreeSWITCH-Hostname: snapshot
FreeSWITCH-Switchname: snapshot
FreeSWITCH-IPv4: 10.11.11.205
FreeSWITCH-IPv6: ::1
Event-Date-Local: 2019-08-19 18:27:16
Event-Date-GMT: Mon, 19 Aug 2019 10:27:16 GMT
Event-Date-Timestamp: 1566210436424976
Event-Calling-File: mod_asr.cpp
Event-Calling-Function: onTranscriptionResultChanged
Event-Calling-Line-Number: 382
Event-Sequence: 1351
UUID: e66e1cf8-6840-4a1e-a44f-173be182bf0f
ASR-Response: {"header":{"namespace":"SpeechTranscriber","name":"TranscriptionResultChanged","status":20000000,"message_id":"435b1bf446354bccb58acd22fa07513d","task_id":"fc56bab0a08f471c88f0dffe50de4111","status_text":"Gateway:SUCCESS:Success."},"payload":{"index":12,"time":78900,"result":"嗯，这目前没有时间给你哦。行行行好嗯好的好","confidence":0.0,"words":[],"status":20000000}}
Channel: sofia/internal/1008@ehuu.com
Timestamp: 20190819182716424976
Answered: true
ASRLeg: bleg


RECV EVENT
Event-Subclass: asr_stop
Event-Name: CUSTOM
Core-UUID: 6882dc67-75c4-4f4f-b767-99c03281a6cc
FreeSWITCH-Hostname: snapshot
FreeSWITCH-Switchname: snapshot
FreeSWITCH-IPv4: 10.11.11.205
FreeSWITCH-IPv6: ::1
Event-Date-Local: 2019-08-19 18:27:17
Event-Date-GMT: Mon, 19 Aug 2019 10:27:17 GMT
Event-Date-Timestamp: 1566210437305129
Event-Calling-File: mod_asr.cpp
Event-Calling-Function: OnChannelClosed
Event-Calling-Line-Number: 456
Event-Sequence: 1358
```

ASR-Response: asr识别返回结果 Channel: 当前Channel Name 
