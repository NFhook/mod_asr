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
originate user/1001 'start_asr:un2wEHfu32KN5ELW Y90VLNxDfMOKWZBqsEJigUn1Wpilbf LTAIwloSbAnFxcWK bleg 300 null' inline
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
订阅`CUSTOM asr` 事件
fs_cli可以通过`/event Custom asr`订阅事件
识别结果通过esl输出

```
RECV EVENT
Event-Subclass: asr
Event-Name: CUSTOM
Core-UUID: d5b1367c-6aa8-43f1-8f6e-5b243755ff64
FreeSWITCH-Hostname: snapshot
FreeSWITCH-Switchname: snapshot
FreeSWITCH-IPv4: 10.11.11.205
FreeSWITCH-IPv6: ::1
Event-Date-Local: 2019-08-15 15:29:09
Event-Date-GMT: Thu, 15 Aug 2019 07:29:09 GMT
Event-Date-Timestamp: 1565854149087801
Event-Calling-File: mod_asr.cpp
Event-Calling-Function: onSentenceEnd
Event-Calling-Line-Number: 279
Event-Sequence: 773
UUID: 227a4d29-604f-4e8d-8067-722ac2bf89f4
ASR-Response: {"header":{"namespace":"SpeechTranscriber","name":"SentenceEnd","status":20000000,"message_id":"a803c11eb2af4d7f9b209be6cdc2c3fb","task_id":"f9de5d4b307344d8b08149270464ea60","status_text":"Gateway:SUCCESS:Success."},"payload":{"index":2,"time":9000,"begin_time":8400,"result":"喂你好。","confidence":0.0,"words":[],"status":20000000,"stash_result":{"sentenceId":0,"beginTime":0,"text":"","currentTime":0}}}
Timestamp: 20190815152909087801
Answered: true
ASRLeg: aleg
```
ASR-Response: asr识别返回结果
