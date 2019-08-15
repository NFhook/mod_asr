# FreeSWITCH ASR模块

[阿里云ASR](https://help.aliyun.com/product/30413.html?spm=a2c4g.11186623.2.10.6b634c07NBBDiY)和FreeSWITCH直接对接，识别结果通过ESL输出
阿里云语音识别SDK [**CppSdk2.0**](http://download.taobaocdn.com/freedom/33762/compress/NlsSdkCpp2.zip?spm=a2c4g.11186623.2.15.35312318Ptx8fD&file=NlsSdkCpp2.zip)

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
```
originate user/1001 'start_asr:un2wEHfu32KN5ELW Y90VLNxDfMOKWZBqsEJigUn1Wpilbf LTAIwloSbAnFxcWK bleg 800 null' inline
```
3. dialplan执行
```
<extension name="asr">
    <condition field="destination_number" expression="^.*$">
        <action application="answer"/>
        <action application="start_asr" data="un2wEHfu32KN5ELW Y90VLNxDfMOKWZBqsEJigUn1Wpilbf LTAIwloSbAnFxcWK aleg 800 null"/>
        <action application="park"/>
    </condition>
</extension>
```
#### 开发
订阅`CUSTOM asr` 事件
fs_cli可以通过`/event Custom asr`订阅事件
识别结果通过esl输出
```
```
