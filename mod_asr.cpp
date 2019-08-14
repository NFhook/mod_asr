#include <switch.h>
#include "nlsClient.h"
#include "nlsEvent.h"
#include "nlsCommonSdk/Token.h"
#include "speechTranscriberRequest.h"

#include <string>
#include <unistd.h>
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#define FRAME_SIZE 3200
#define SAMPLE_RATE 8000

using std::map;
using std::string;
using std::vector;
//using std::cout;
//using std::endl;
using std::ifstream;
using std::ios;

using namespace AlibabaNlsCommon;

using AlibabaNls::NlsClient;
using AlibabaNls::NlsEvent;
using AlibabaNls::LogDebug;
using AlibabaNls::LogInfo;
using AlibabaNls::SpeechTranscriberCallback;
using AlibabaNls::SpeechTranscriberRequest;

SWITCH_MODULE_LOAD_FUNCTION(mod_asr_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_asr_shutdown);

extern "C" {
     SWITCH_MODULE_DEFINITION(mod_asr, mod_asr_load, mod_asr_shutdown, NULL);
};

// 自定义事件回调参数
struct ParamCallBack {
    int iTimeFlag;
	char sUUID[100];
    bool bSend;
    int  iFrame;
};

typedef struct {
    switch_core_session_t   *session;
    switch_media_bug_t      *bug;
    //switch_mutex_t          *mutex;
    pthread_mutex_t         mutex;
    SpeechTranscriberRequest *request;
	SpeechTranscriberCallback *callback;
	ParamCallBack cbParam;
    char                    *id;
    char                    *seceret;
    char                    *accessid;
    char                    tokenid[100];
    int                     tokentime;
    int                     iSilence;
    char                    *leg;
    int                     stop;
    char                    *sUrl;

    char                    framedata[2*1024*1024];
    int                     datalen;
    int                     datatotal;
    int                     framelen;
} switch_da_t;



void wavWrite_int16(char *filename, char *buffer, int sampleRate, uint32_t totalSampleCount) {
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = (drwav_uint32) sampleRate;
    format.bitsPerSample = 16;
    drwav *pWav = drwav_open_file_write(filename, &format);
    if (pWav) {
        drwav_uint64 samplesWritten = drwav_write_raw(pWav, totalSampleCount, buffer);
        drwav_uninit(pWav);
        if (samplesWritten != totalSampleCount) {
            fprintf(stderr, "ERROR.\n");
            exit(1);
        }
    }
}

std::string ToBinaryString(const char* buf,int len)
{
    int output_len = len*8;
    std::string output;
    const char* m[] = {"0","1"};

    for(int i = output_len - 1,j = 0; i >=0 ; --i,++j)
    {
        output.append(m[((char)buf[j/8] >> (i % 8)) & 0x01],1);
    }
    return output;
}

std::string ToHexString(const uint8_t* buf,int len)
{
    std::string output;
    char temp[8];
    for (int i = 0; i < len; ++i)
    {
        sprintf(temp,"%.2x",(uint8_t)buf[i]);
        output.append(temp,2);
        //output.append(tok);
    }

    return output;
}

bool CheckData(const uint8_t* buf,int len)
{
    int iCounterF = 0;
    int iCounter0 = 0;
    bool bRet = true;
    for (int i = 0; i < len; ++i)
    {
        if ((uint8_t)buf[i] == 0xf)
        {
            iCounterF++;
            if (iCounterF > len/2)
            {
                bRet = false;
                break;
            }
        }
        if ((uint8_t)buf[i] == 0x0)
        {
            iCounter0++;
            if (iCounter0 > len/2)
            {
                bRet = false;
                break;
            }
        }
    }
    return bRet;
}


int getTokenId(switch_da_t *pvt) {
    NlsToken nlsTokenRequest;
    switch_time_t now = switch_micro_time_now()/1000000;
    if (now > pvt->tokentime)
    {
        /*设置阿里云账号KeySecret*/
        nlsTokenRequest.setKeySecret(pvt->seceret);
        /*设置阿里云账号KeyId*/
        nlsTokenRequest.setAccessKeyId(pvt->accessid);

        /*获取token. 成功返回0, 失败返回-1*/
        if (-1 == nlsTokenRequest.applyNlsToken()) {
            //cout << "Failed: " << nlsTokenRequest.getErrorMsg() << endl; /*获取失败原因*/
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, " GetTokenFailed  %s\n", nlsTokenRequest.getErrorMsg());
            return -1;
        } else {
           // cout << "TokenId: " << nlsTokenRequest.getToken() << endl; /*获取TokenId*/
           // cout << "TokenId expireTime: " << nlsTokenRequest.getExpireTime() << endl; /*获取Token有效期时间戳(秒)*/
            memcpy(pvt->tokenid,nlsTokenRequest.getToken(),100);
            pvt->tokentime = nlsTokenRequest.getExpireTime();
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "GetToken:%s  %d\n", pvt->tokenid,pvt->tokentime);
            return 0;
        }
    }else
    {
        return 0;
    }
}

/**
 * @brief 服务端检测到了一句话结束, sdk内部线程上报SentenceEnd事件
 * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */
void onSentenceEnd(NlsEvent* cbEvent, void* cbParam) {
    char filename[100];
    char answered[10];
    char filepath[150];
    char currtime[50];
    char currpath[100];
    char fullpath[200];
    //char channelname[100];
    ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    getcwd(currpath,sizeof(currpath));
    switch_time_exp_t tm;
    switch_time_t now = switch_micro_time_now();
    switch_time_exp_lt(&tm, now);
    switch_snprintf(currtime, sizeof(currtime), "%0.4d%0.2d%0.2d%0.2d%0.2d%0.2d%0.6d",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_usec);
    switch_snprintf(filepath, sizeof(filepath), "../recordings/");
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "----------------------------------2:%s\n", cbEvent->getAllResponse());
    cJSON *cj, *cjp, *cjc1, *cjc2, *cjc3;
    int nflag = 0;
    int nlen = 0;
    int nevent = 0;
    if (!(cj = cJSON_Parse(cbEvent->getAllResponse()))) {
        //return SWITCH_STATUS_FALSE;
    }else
    {
        for (cjp = cj->child; cjp; cjp = cjp->next) {
            //char *name = cjp->string;
            //char *value = cjp->valuestring;
          //cjp = cj->child;
            switch(cjp->type)
            {
                case cJSON_False:
                    break;
                case cJSON_True:
                    break;
                case cJSON_NULL:
                    break;
                case cJSON_Number:
                    break;
                case cJSON_String:
                    break;
                case cJSON_Array:
                    break;
                case cJSON_Object:
                {
                    switch_da_t *pvt;
                    switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
                    switch_channel_t *channel = switch_core_session_get_channel(ses);
                    if(pvt = (switch_da_t*)switch_channel_get_private(channel, "asr"))
                    {
                        //if (cjc1 = cJSON_GetObjectItem(cjp, "payload")) {
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " OnResultDataRecved %s %s %s\n", tmpParam->sUUID, cbEvent->getResult(),currtime);
                        if ((cjc2 = cJSON_GetObjectItem(cjp, "begin_time")) && (cjc3 = cJSON_GetObjectItem(cjp, "time"))) {
                            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "***begin_time: %d  time: %d\n", cjc2->valueint, cjc3->valueint);
                            if(ses)
                            {
                                char *argv[10];
                                char *dup = NULL;
                                dup = strdup(switch_channel_get_name(channel));
                                int argc = switch_split(dup, '/', argv);
                                //switch_snprintf(channelname, sizeof(channelname), "%s",dup);
                                if (argc > 0)
                                {
                                    switch_snprintf(filename, sizeof(filename), "%s-%s-%d.wav",argv[argc-1],currtime,pvt->datalen);
                                }else
                                {
                                    switch_snprintf(filename, sizeof(filename), "00000000000-%s-%d.wav",currtime,pvt->datalen);
                                }

                                switch_snprintf(fullpath, sizeof(fullpath), "%s/%s",SWITCH_GLOBAL_dirs.recordings_dir,filename);
                                //switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "-----recordpath-----------------------------:%s\n", fullpath);

                                //switch_mutex_lock(pvt->mutex);
                                //pthread_mutex_lock(&(pvt->mutex));
                                nflag = (cjc2->valueint * pvt->framelen)/20 - pvt->datatotal;
                                nlen = ((cjc3->valueint - cjc2->valueint) * pvt->framelen)/20;
                                tmpParam->iTimeFlag = cjc3->valueint;
                                //pthread_mutex_unlock(&(pvt->mutex));

                                wavWrite_int16(fullpath, pvt->framedata+nflag, 8000, (uint32_t) nlen);
                                if(switch_channel_test_flag(channel, CF_ANSWERED) != 0)
                                {
                                    switch_snprintf(answered, sizeof(answered), "true");
                                }else
                                {
                                    switch_snprintf(answered, sizeof(answered), "false");
                                }
                                //switch_mutex_unlock(pvt->mutex);


                            }
                            //switch_core_session_rwunlock(ses);
                            switch_event_t *event = NULL;
                            if (switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
                                event->subclass_name = strdup("EV_ALIASR");
                                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Event-Subclass", event->subclass_name);
                                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "UUID", tmpParam->sUUID);
                                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASR-Response", cbEvent->getAllResponse());
                                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Timestamp",currtime);
                                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "FilePath",fullpath);
                                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "FileName",filename);
                                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Answered",answered);

                                //switch_mutex_lock(pvt->mutex);
                                //pthread_mutex_lock(&(pvt->mutex));
                                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASRLeg",pvt->leg);
                                //switch_mutex_unlock(pvt->mutex);
                                //pthread_mutex_unlock(&(pvt->mutex));

                                switch_event_fire(&event);
                                nevent = 1;
                            }
                        }
                    }
                    switch_core_session_rwunlock(ses);
                    break;
                }
                default:
                    break;
            }
            if (1 == nevent){
                break;
            }
        }
    }
    cJSON_Delete(cj);
}


/**
 * @brief 识别结果发生了变化, sdk在接收到云端返回到最新结果时, sdk内部线程上报ResultChanged事件
 * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */
void onTranscriptionResultChanged(NlsEvent* cbEvent, void* cbParam) {
    ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    char filename[100];
    char answered[10];
    char filepath[150];
    char currtime[50];
    char currpath[100];
    char fullpath[200];
    //char channelname[100];
    getcwd(currpath,sizeof(currpath));
    switch_time_exp_t tm;
    switch_time_t now = switch_micro_time_now();
    switch_time_exp_lt(&tm, now);
    switch_snprintf(currtime, sizeof(currtime), "%0.4d%0.2d%0.2d%0.2d%0.2d%0.2d%0.6d",tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_usec);
    switch_snprintf(filepath, sizeof(filepath), "../recordings/");
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "----------------------------------1:%s\n", cbEvent->getAllResponse());
    cJSON *cj, *cjp, *cjc1, *cjc2, *cjc3;
    int nflag = 0;
    int nlen = 0;
    if (!(cj = cJSON_Parse(cbEvent->getAllResponse()))) {
        //return SWITCH_STATUS_FALSE;
    }else
    {
        for (cjp = cj->child; cjp; cjp = cjp->next) {
            //char *name = cjp->string;
            //char *value = cjp->valuestring;
            switch(cjp->type)
            {
                case cJSON_False:
                    break;
                case cJSON_True:
                    break;
                case cJSON_NULL:
                    break;
                case cJSON_Number:
                    break;
                case cJSON_String:
                    break;
                case cJSON_Array:
                    break;
                case cJSON_Object:
                {
                    switch_da_t *pvt;
                    switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
                    switch_channel_t *channel = switch_core_session_get_channel(ses);
                    if(pvt = (switch_da_t*)switch_channel_get_private(channel, "asr"))
                    {
                        if(ses)
                        {
                            if(switch_channel_test_flag(channel, CF_ANSWERED) != 0)
                            {
                                switch_snprintf(answered, sizeof(answered), "true");
                            }else
                            {
                                switch_snprintf(answered, sizeof(answered), "false");
                            }
                        }
                        switch_event_t *event = NULL;
                        if (switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
                            event->subclass_name = strdup("EV_ALIASR_0");
                            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Event-Subclass", event->subclass_name);
                            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "UUID", tmpParam->sUUID);
                            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASR-Response", cbEvent->getAllResponse());
                            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Timestamp",currtime);
                            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Answered",answered);
                            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "FilePath","");
                            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "FileName","");

                            //switch_mutex_lock(pvt->mutex);
                           // pthread_mutex_lock(&(pvt->mutex));
                            switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASRLeg",pvt->leg);
                            //switch_mutex_unlock(pvt->mutex);
                           // pthread_mutex_unlock(&(pvt->mutex));

                            switch_event_fire(&event);
                        }
                    }
                    switch_core_session_rwunlock(ses);
                break;
                }
                default:
                    break;
            }
        }
    }
    cJSON_Delete(cj);
}

/**
 * @brief 调用start(), 成功与云端建立连接, sdk内部线程上报started事件
 * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */
void onTranscriptionStarted(NlsEvent* cbEvent, void* cbParam) {
    ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, " onTranscriptionStarted %s %s %s\n",tmpParam->sUUID, cbEvent->getTaskId(), cbEvent->getResult());
    switch_da_t *pvt = NULL;

    switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
    switch_channel_t *channel = switch_core_session_get_channel(ses);
    if(pvt = (switch_da_t*)switch_channel_get_private(channel, "asr"))
    {
        //switch_mutex_lock(pvt->mutex);
        //pthread_mutex_lock(&(pvt->mutex));
        pvt->cbParam.bSend = true;
        //switch_mutex_unlock(pvt->mutex);
        //pthread_mutex_unlock(&(pvt->mutex));
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, " onTranscriptionStarted Allow send audio  %s %s\n", cbEvent->getTaskId(), cbEvent->getResult());
    }else
    {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, " onTranscriptionStarted Failed %s %s\n", tmpParam->sUUID, cbEvent->getErrorMessage());
    }
    switch_core_session_rwunlock(ses);
}

/**
 * @brief 识别结束或发生异常时，会关闭连接通道, sdk内部线程上报ChannelCloseed事件
 * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */
void OnChannelClosed(NlsEvent* cbEvent, void* cbParam)
{
    ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, " OnChannelCloseed %s %s %s\n",tmpParam->sUUID, cbEvent->getTaskId(), cbEvent->getResult());

    switch_event_t *event = NULL;
    if (switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
        event->subclass_name = strdup("asr");
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Event-Subclass", event->subclass_name);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASR-Close", cbEvent->getResult());
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Channel", cbEvent->getTaskId());
        switch_event_fire(&event);
    }
}




/**
 * @brief 服务端停止实时音频流识别时, sdk内部线程上报Completed事件
 * @note 上报Completed事件之后，SDK内部会关闭识别连接通道. 此时调用sendAudio会返回-1, 请停止发送.
 *       不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */
void onTranscriptionCompleted(NlsEvent* cbEvent, void* cbParam) {
    ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, " onTranscriptionCompleted1 %s %s %s\n", tmpParam->sUUID, cbEvent->getTaskId(), cbEvent->getResult());
    switch_da_t *pvt;
    switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
    switch_channel_t *channel = switch_core_session_get_channel(ses);

    if (pvt = (switch_da_t*)switch_channel_get_private(channel, "asr"))
    {
        //switch_mutex_lock(pvt->mutex);
        // pthread_mutex_lock(&(pvt->mutex));
        pvt->cbParam.bSend = false;
        //switch_mutex_unlock(pvt->mutex);
        //pthread_mutex_unlock(&(pvt->mutex));
    }else
    {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, " onTranscriptionCompleted Failed %s %s\n", tmpParam->sUUID, cbEvent->getErrorMessage());
    }
    switch_core_session_rwunlock(ses);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, " onTranscriptionCompleted2 %s %s\n", cbEvent->getTaskId(), cbEvent->getResult());
}

void onSentenceBegin(NlsEvent* cbEvent, void* cbParam) {
    ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, " onSentenceBegin %s %s\n", cbEvent->getTaskId(), cbEvent->getResult());
}


/**
 * @brief 识别过程(包含start(), send(), stop())发生异常时, sdk内部线程上报TaskFailed事件
 * @note 不允许在回调函数内部调用stop(), releaseTranscriberRequest()对象操作, 否则会异常
 * @note 上报TaskFailed事件之后, SDK内部会关闭识别连接通道. 此时调用sendAudio会返回-1, 请停止发送
 * @param cbEvent 回调事件结构, 详见nlsEvent.h
 * @param cbParam 回调自定义参数，默认为NULL, 可以根据需求自定义参数
 * @return
 */
void onTaskFailed(NlsEvent* cbEvent, void* cbParam) {
    ParamCallBack* tmpParam = (ParamCallBack*)cbParam;
    switch_da_t *pvt;
    switch_core_session_t *ses = switch_core_session_force_locate(tmpParam->sUUID);
    switch_channel_t *channel = switch_core_session_get_channel(ses);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, " OnOperationFailed %s %s\n", tmpParam->sUUID, cbEvent->getErrorMessage());
    if (pvt = (switch_da_t*)switch_channel_get_private(channel, "asr"))
    {
        //switch_mutex_lock(pvt->mutex);
        //pthread_mutex_lock(&(pvt->mutex));
        pvt->cbParam.bSend = false;
        //switch_mutex_unlock(pvt->mutex);
        //pthread_mutex_unlock(&(pvt->mutex));
       // pvt->datalen = 0;
       // pvt->datatotal = 0;
      //  pvt->framelen = 0;

        pvt->callback = new SpeechTranscriberCallback();
        pvt->callback->setOnTranscriptionStarted(onTranscriptionStarted, &pvt->cbParam); // 设置识别启动回调函数
        pvt->callback->setOnTranscriptionResultChanged(onTranscriptionResultChanged, &pvt->cbParam); // 设置识别结果变化回调函数
        pvt->callback->setOnTranscriptionCompleted(onTranscriptionCompleted, &pvt->cbParam); // 设置语音转写结束回调函数
        pvt->callback->setOnSentenceBegin(onSentenceBegin, &pvt->cbParam); // 设置一句话开始回调函数
        pvt->callback->setOnSentenceEnd(onSentenceEnd, &pvt->cbParam); // 设置一句话结束回调函数
        pvt->callback->setOnTaskFailed(onTaskFailed, &pvt->cbParam); // 设置异常识别回调函数
        pvt->callback->setOnChannelClosed(OnChannelClosed, &pvt->cbParam); // 设置识别通道关闭回调函数
        pvt->request = NlsClient::getInstance()->createTranscriberRequest(pvt->callback);

        if (pvt->request) {
            pvt->request->setAppKey(pvt->id); // 设置AppKey, 必填参数, 请参照官网申请
            pvt->request->setFormat("pcm"); // 设置音频数据编码格式, 可选参数, 目前支持pcm, opus, opu. 默认是pcm
            pvt->request->setSampleRate(SAMPLE_RATE); // 设置音频数据采样率, 可选参数, 目前支持16000, 8000. 默认是16000
            pvt->request->setIntermediateResult(true); // 设置是否返回中间识别结果, 可选参数. 默认false
            pvt->request->setPunctuationPrediction(true); // 设置是否在后处理中添加标点, 可选参数. 默认false
            pvt->request->setInverseTextNormalization(true); // 设置是否在后处理中执行ITN, 可选参数. 默认false
            pvt->request->setSemanticSentenceDetection(true); // 设置是否语义断句, 可选参数. 默认false
            pvt->request->setMaxSentenceSilence(pvt->iSilence);
            pvt->request->setToken(pvt->tokenid); // 设置账号校验token, 必填参数
            if (switch_string_match(pvt->sUrl ,sizeof(pvt->sUrl),"null",4))
            {
                pvt->request->setUrl(pvt->sUrl);
            }
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " onTaskFailed --- ASR SetUrl: %s token : %s\n", pvt->sUrl,pvt->tokenid);

            if (pvt->request->start() < 0)
            {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "onTaskFailed --- ASR Start Failed channel:%s\n", switch_channel_get_name(channel));
                if (switch_channel_up(channel)) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "onTaskFailed --- Releasing channel : %s\n", switch_channel_get_name(channel));
                    switch_channel_hangup(channel, SWITCH_CAUSE_NORMAL_CLEARING);
                }else
                {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "onTaskFailed --- Channel has been released : %s\n", switch_channel_get_name(channel));
                }
                //NlsClient::getInstance()->releaseTranscriberRequest(pvt->request); // start()失败，释放request对象
                //delete pvt->callback;
                //pvt->callback = NULL;
            }
            else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "onTaskFailed --- ASR Start Succeed channel:%s\n", switch_channel_get_name(channel));
                pvt->cbParam.bSend = true;
            }
        }
      /*  if (pvt->request->start() < 0)
        {
            pvt->cbParam.bSend = false;
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "onTaskFailed --- ASR Start Failed channel:%s\n", switch_channel_get_name(channel));
        }
        else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "onTaskFailed --- ASR Start Succeed channel:%s\n", switch_channel_get_name(channel));
            pvt->cbParam.bSend = true;
        }*/

    }else
    {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "onTaskFailed --- get pvt error : %s\n", switch_channel_get_name(channel));
        if (switch_channel_up(channel)) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "onTaskFailed --- Releasing channel : %s\n", switch_channel_get_name(channel));
            switch_channel_hangup(channel, SWITCH_CAUSE_NORMAL_CLEARING);
        }else
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "onTaskFailed --- Channel has been released : %s\n", switch_channel_get_name(channel));
        }
    }
    switch_core_session_rwunlock(ses);

   /* switch_event_t *event = NULL;
    if (switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
        event->subclass_name = strdup("asr");
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Event-Subclass", event->subclass_name);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASR-Error", cbEvent->getErrorMessage());
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Channel", tmpParam->sUUID);
        switch_event_fire(&event);
    }*/
}


static switch_bool_t asr_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
    switch_da_t *pvt = (switch_da_t *)user_data;
    switch_channel_t *channel = switch_core_session_get_channel(pvt->session);

    switch (type) {
        case SWITCH_ABC_TYPE_INIT:
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "ASR Channel Init:%s\n", switch_channel_get_name(channel));
        }
        break;
        case SWITCH_ABC_TYPE_CLOSE:
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "ASR Close Succeed channel1:%s\n", switch_channel_get_name(channel));
            //switch_mutex_lock(pvt->mutex);
            //pthread_mutex_lock(&(pvt->mutex));
            pvt->cbParam.bSend = false;
            //pthread_mutex_unlock(&(pvt->mutex));
            if (pvt->request) {
                pvt->request->stop();
                NlsClient::getInstance()->releaseTranscriberRequest(pvt->request); // start()失败，释放request对象
                delete pvt->callback;
                pvt->callback = NULL;
                switch_channel_set_private(channel, "asr", NULL);
            }
            //switch_mutex_unlock(pvt->mutex);

            //switch_core_media_bug_remove(pvt->session, &pvt->bug);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "ASR Close Succeed channel2:%s\n", switch_channel_get_name(channel));
            return SWITCH_FALSE;
        }
        break;
        case SWITCH_ABC_TYPE_WRITE_REPLACE:
        {
            //switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "ASR *************SWITCH_ABC_TYPE_WRITE_REPLACE***************1:%s\n", switch_channel_get_name(channel));
            switch_frame_t *frame;

            if (switch_string_match(pvt->leg ,sizeof(pvt->leg),"aleg",4))
            {
                //switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "ASRBLeg:%s\n", pvt->leg);
                frame = switch_core_media_bug_get_write_replace_frame(bug);
                if (frame) {
                    char*frame_data = (char*)frame->data;
                    uint8_t*frame_data1 = (uint8_t*)frame->data;
                    int frame_len = frame->datalen;
                    switch_core_media_bug_set_write_replace_frame(bug, frame);
                    if (frame->channels != 1)
                    {
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "nonsupport channels:%d!\n",frame->channels);
                        return SWITCH_FALSE;
                    }
                    //switch_mutex_lock(pvt->mutex);
                    if (pvt->cbParam.bSend)
                    {
                        if (pvt->request) {
                            //switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, " frame %s \n", ToHexString(frame_data1,frame_len).c_str());
                            if (pvt->datalen+frame_len > 2*1024*1024)
                            {
                                pvt->datalen = pvt->datalen - 1024*1024;
                                pvt->datatotal = pvt->datatotal + 1024*1024;
                                memcpy(pvt->framedata,pvt->framedata+1024*1024,1024*1024);
                                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "------------------Framedate move 1024*1024:%s\n", switch_channel_get_name(channel));
                            }
                            memcpy((pvt->framedata + pvt->datalen),frame_data,frame_len);
                            pvt->datalen = pvt->datalen + frame_len;
                            pvt->framelen = frame_len;
                            if(CheckData(frame_data1,frame_len))
                            {
                                if (pvt->request->sendAudio(frame_data, frame_len) <= 0) {
                                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "SendAudio  Failed:%s framedata:%s\n", switch_channel_get_name(channel), ToHexString(frame_data1,frame_len).c_str());
                                   // pthread_mutex_lock(&(pvt->mutex));
                                    pvt->cbParam.bSend = false;
                                    //pthread_mutex_unlock(&(pvt->mutex));
                                    //pvt->request->stop();
                                    //NlsClient::getInstance()->releaseTranscriberRequest(pvt->request); // start()失败，释放request对象
                                   // delete pvt->callback;
                                    //pvt->callback = NULL;
                                   /* if (switch_channel_up(channel)) {
                                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "asr_callback --- Releasing channel : %s\n", switch_channel_get_name(channel));
                                        switch_channel_hangup(channel, SWITCH_CAUSE_NORMAL_CLEARING);
                                    }else
                                    {
                                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "asr_callback --- Channel has been released : %s\n", switch_channel_get_name(channel));
                                    }*/
                                }
                            }else
                            {
                                //switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "SendAudio  Failed---:%s framedata:%s\n", switch_channel_get_name(channel), ToHexString(frame_data1,frame_len).c_str());
                            }
                        }
                    }
                    //switch_mutex_unlock(pvt->mutex);

                }
            }
        }
        break;
        case SWITCH_ABC_TYPE_READ_REPLACE:
        {
            //switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "ASR ************SWITCH_ABC_TYPE_READ_REPLACE****************1:%s\n", switch_channel_get_name(channel));
            switch_frame_t *frame;

            if (switch_string_match(pvt->leg ,sizeof(pvt->leg),"aleg",4))
            {

            }else
            {
                frame = switch_core_media_bug_get_read_replace_frame(bug);
                if (frame) {
                    char*frame_data = (char*)frame->data;
                    uint8_t*frame_data1 = (uint8_t*)frame->data;
                    int frame_len = frame->datalen;
                    switch_core_media_bug_set_read_replace_frame(bug, frame);
                    if (frame->channels != 1)
                    {
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "nonsupport channels:%d!\n",frame->channels);
                        return SWITCH_FALSE;
                    }
                    //switch_mutex_lock(pvt->mutex);

                    if (pvt->cbParam.bSend)
                    {
                        if (pvt->request) {
                            //switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, " frame %s \n", ToHexString(frame_data1,frame_len).c_str());
                            if (pvt->datalen+frame_len > 2*1024*1024)
                            {
                                pvt->datalen = pvt->datalen - 1024*1024;
                                pvt->datatotal = pvt->datatotal + 1024*1024;
                                memcpy(pvt->framedata,pvt->framedata+1024*1024,1024*1024);
                                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "------------------Framedate move 1024*1024:%s\n", switch_channel_get_name(channel));
                            }
                            memcpy((pvt->framedata + pvt->datalen),frame_data,frame_len);
                            pvt->datalen = pvt->datalen + frame_len;
                            pvt->framelen = frame_len;
                            if(CheckData(frame_data1,frame_len))
                            {
                                if (pvt->request->sendAudio(frame_data, frame_len) <= 0) {
                                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "SendAudio  Failed:%s  framedata: %s\n", switch_channel_get_name(channel),ToHexString(frame_data1,frame_len).c_str());
                                    //pthread_mutex_lock(&(pvt->mutex));
                                    pvt->cbParam.bSend = false;
                                    //pthread_mutex_unlock(&(pvt->mutex));
                                    //pvt->request->stop();
                                    //NlsClient::getInstance()->releaseTranscriberRequest(pvt->request); // start()失败，释放request对象
                                    //delete pvt->callback;
                                    //pvt->callback = NULL;
                                   /* if (switch_channel_up(channel)) {
                                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "asr_callback --- Releasing channel : %s\n", switch_channel_get_name(channel));
                                        switch_channel_hangup(channel, SWITCH_CAUSE_NORMAL_CLEARING);
                                    }else
                                    {
                                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "asr_callback --- Channel has been released : %s\n", switch_channel_get_name(channel));
                                    }*/
                                }
                            }else
                            {
                               // switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "SendAudio  Failed---:%s  framedata: %s\n", switch_channel_get_name(channel),ToHexString(frame_data1,frame_len).c_str());

                            }
                        }
                    }
                    //switch_mutex_unlock(pvt->mutex);
                }
            }
        }
        break;
        default: break;
    }
    return SWITCH_TRUE;
}


SWITCH_STANDARD_APP(stop_asr_session_function)
{
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "stop_asr***1\n");
    switch_da_t *pvt;
    switch_channel_t *channel = switch_core_session_get_channel(session);
    if ((pvt = (switch_da_t*)switch_channel_get_private(channel, "asr"))) {
        //switch_core_media_bug_remove(session, &pvt->bug);
        switch_channel_set_private(channel, "asr", NULL);
    }
     switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "stop_asr***2\n");
}


SWITCH_STANDARD_APP(start_asr_session_function)
{
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_status_t status;
    switch_da_t *pvt;

    switch_codec_implementation_t read_impl;
    memset(&read_impl, 0, sizeof(switch_codec_implementation_t));

    char *argv[6] = { 0 };
    int argc;
    char *lbuf = NULL;

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "%s Start ASR\n", switch_channel_get_name(channel));
    if (!zstr(data) && (lbuf = switch_core_session_strdup(session, data))
        && (argc = switch_separate_string(lbuf, ' ', argv, (sizeof(argv) / sizeof(argv[0])))) >= 6) {
        switch_core_session_get_read_impl(session, &read_impl);
        if (!(pvt = (switch_da_t*)switch_core_session_alloc(session, sizeof(switch_da_t)))) {
            return;
        }
        //switch_mutex_init(&pvt->mutex, SWITCH_MUTEX_DEFAULT, switch_core_session_get_pool(session));
        //pthread_mutex_init(&(pvt->mutex), NULL);
        pvt->stop = 0;
        pvt->session = session;
        pvt->id = argv[0];
        pvt->seceret = argv[1];
        pvt->accessid = argv[2];
        pvt->leg = argv[3];
        pvt->iSilence = atoi(argv[4]);
        pvt->sUrl = argv[5];
        pvt->datalen = 0;
        pvt->datatotal = 0;
        pvt->framelen = 0;
        memset(pvt->tokenid, 0, 100);
        pvt->tokentime = 0;
        pvt->cbParam.bSend = false;
        pvt->cbParam.iTimeFlag = 0;

        memset(pvt->cbParam.sUUID, 0, 100);
        char *buf = switch_channel_get_uuid(channel);
        memcpy(pvt->cbParam.sUUID,buf,100);

        if ((status = switch_core_media_bug_add(session, "asr", NULL,asr_callback, pvt, 0, SMBF_READ_REPLACE | SMBF_WRITE_REPLACE | SMBF_NO_PAUSE | SMBF_ONE_ONLY, &(pvt->bug))) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s Start ASR Faild!\n", switch_channel_get_name(channel));
        }
        switch_channel_set_private(channel, "asr", pvt);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "%s switch_core_media_bug_add OK!\n", switch_channel_get_name(channel));

        if (switch_string_match(pvt->sUrl ,sizeof(pvt->sUrl),"null",4))
        {
            memcpy(pvt->tokenid,"default",7);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " ASR GetToken URL: %s Token : %s\n", pvt->sUrl,pvt->tokenid);
        }else{
            if(getTokenId(pvt) == -1){
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " ASR GetToken Faild %s\n", pvt->sUrl);
            }
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " ASR GetToken URL: %s Token : %s\n", pvt->sUrl,pvt->tokenid);
        }
        //switch_mutex_init(&g_mutex, SWITCH_MUTEX_NESTED, pool);//
        pvt->callback = new SpeechTranscriberCallback();
        pvt->callback->setOnTranscriptionStarted(onTranscriptionStarted, &pvt->cbParam); // 设置识别启动回调函数
        pvt->callback->setOnTranscriptionResultChanged(onTranscriptionResultChanged, &pvt->cbParam); // 设置识别结果变化回调函数
        pvt->callback->setOnTranscriptionCompleted(onTranscriptionCompleted, &pvt->cbParam); // 设置语音转写结束回调函数
        pvt->callback->setOnSentenceBegin(onSentenceBegin, &pvt->cbParam); // 设置一句话开始回调函数
        pvt->callback->setOnSentenceEnd(onSentenceEnd, &pvt->cbParam); // 设置一句话结束回调函数
        pvt->callback->setOnTaskFailed(onTaskFailed, &pvt->cbParam); // 设置异常识别回调函数
        pvt->callback->setOnChannelClosed(OnChannelClosed, &pvt->cbParam); // 设置识别通道关闭回调函数
        pvt->request = NlsClient::getInstance()->createTranscriberRequest(pvt->callback);

        if (pvt->request) {
            pvt->request->setAppKey(pvt->id); // 设置AppKey, 必填参数, 请参照官网申请
            pvt->request->setFormat("pcm"); // 设置音频数据编码格式, 可选参数, 目前支持pcm, opus, opu. 默认是pcm
            pvt->request->setSampleRate(SAMPLE_RATE); // 设置音频数据采样率, 可选参数, 目前支持16000, 8000. 默认是16000
            pvt->request->setIntermediateResult(true); // 设置是否返回中间识别结果, 可选参数. 默认false
            pvt->request->setPunctuationPrediction(false); // 设置是否在后处理中添加标点, 可选参数. 默认false
            pvt->request->setInverseTextNormalization(false); // 设置是否在后处理中执行ITN, 可选参数. 默认false
            pvt->request->setSemanticSentenceDetection(false); // 设置是否语义断句, 可选参数. 默认false
            pvt->request->setMaxSentenceSilence(pvt->iSilence); //语音断句检测阈值，一句话之后静音长度超过该值，即本句结束，合法参数范围200～2000(ms)，默认值800ms
            pvt->request->setToken(pvt->tokenid); // 设置账号校验token, 必填参数
            if (switch_string_match(pvt->sUrl ,sizeof(pvt->sUrl),"null",4))
            {
                pvt->request->setUrl(pvt->sUrl);
            }
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " ASR SetUrl: %s token : %s\n", pvt->sUrl,pvt->tokenid);

            if (pvt->request->start() < 0)
            {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "ASR Start Failed channel:%s\n", switch_channel_get_name(channel));
                NlsClient::getInstance()->releaseTranscriberRequest(pvt->request); // start()失败，释放request对象
                delete pvt->callback;
                pvt->callback = NULL;
            }
            else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "ASR Start Succeed channel:%s\n", switch_channel_get_name(channel));
                pvt->cbParam.bSend = true;
            }
        }
    }
    else {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING, "%s id or secret can not be empty\n", switch_channel_get_name(channel));
    }
    return;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_asr_load)
{
    switch_application_interface_t *app_interface;
    //switch_core_new_memory_pool(&pool);
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);


    // 根据需要设置SDK输出日志, 可选. 此处表示SDK日志输出至log-Transcriber.txt， LogDebug表示输出所有级别日志
    int ret = NlsClient::getInstance()->setLogConfig("log-transcriber.txt", LogDebug);
    if (-1 == ret) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "set log failed.\n");
        //cout << "set log failed." << endl;
        //return -1;
    }

    SWITCH_ADD_APP(app_interface, "start_asr", "asr", "asr",start_asr_session_function, "", SAF_MEDIA_TAP);
    SWITCH_ADD_APP(app_interface, "stop_asr", "asr", "asr", stop_asr_session_function, "", SAF_NONE);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " asr_load\n");

    return SWITCH_STATUS_SUCCESS;
}


 SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_asr_shutdown)
{
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, " asr_shutdown\n");

    return SWITCH_STATUS_SUCCESS;
}
