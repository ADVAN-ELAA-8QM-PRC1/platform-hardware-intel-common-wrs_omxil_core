/*
 * Copyright (C) 2009 Wind River Systems.
 */

#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <OMX_Core.h>
#include <OMX_Component.h>

#include <componentbase.h>

#include <queue.h>
#include <workqueue.h>

#define LOG_TAG "componentbase"
#include <log.h>

/*
 * CmdProcessWork
 */
CmdProcessWork::CmdProcessWork(CmdHandlerInterface *ci)
{
    this->ci = ci;

    workq = new WorkQueue;

    __queue_init(&q);
    pthread_mutex_init(&lock, NULL);
}

CmdProcessWork::~CmdProcessWork()
{
    workq->FlushWork();
    delete workq;

    pthread_mutex_lock(&lock);
    queue_free_all(&q);
    pthread_mutex_unlock(&lock);

    pthread_mutex_destroy(&lock);
}

OMX_ERRORTYPE CmdProcessWork::PushCmdQueue(struct cmd_s *cmd)
{
    int ret;

    pthread_mutex_lock(&lock);
    ret = queue_push_tail(&q, cmd);
    if (ret) {
        pthread_mutex_unlock(&lock);
        return OMX_ErrorInsufficientResources;
    }

    workq->ScheduleWork(this);
    pthread_mutex_unlock(&lock);

    return OMX_ErrorNone;
}

struct cmd_s *CmdProcessWork::PopCmdQueue(void)
{
    struct cmd_s *cmd;

    pthread_mutex_lock(&lock);
    cmd = (struct cmd_s *)queue_pop_head(&q);
    pthread_mutex_unlock(&lock);

    return cmd;
}

void CmdProcessWork::ScheduleIfAvailable(void)
{
    bool avail;

    pthread_mutex_lock(&lock);
    avail = queue_length(&q) ? true : false;
    pthread_mutex_unlock(&lock);

    if (avail)
        workq->ScheduleWork(this);
}

void CmdProcessWork::Work(void)
{
    struct cmd_s *cmd;

    cmd = PopCmdQueue();
    if (cmd) {
        ci->CmdHandler(cmd);
        free(cmd);
    }
    ScheduleIfAvailable();
}

/* end of CmdProcessWork */

/*
 * ComponentBase
 */
/*
 * constructor & destructor
 */
void ComponentBase::__ComponentBase(void)
{
    memset(name, 0, OMX_MAX_STRINGNAME_SIZE);
    cmodule = NULL;
    handle = NULL;

    roles = NULL;
    nr_roles = 0;

    ports = NULL;
    nr_ports = 0;
    memset(&portparam, 0, sizeof(portparam));

    state = OMX_StateUnloaded;

    cmdwork = new CmdProcessWork(this);
}

ComponentBase::ComponentBase()
{
    __ComponentBase();
}

ComponentBase::ComponentBase(const OMX_STRING name)
{
    __ComponentBase();
    SetName(name);
}

ComponentBase::~ComponentBase()
{
    delete cmdwork;

    if (roles) {
        OMX_U32 i;

        for (i = 0; i < nr_roles; i++)
            free(roles[i]);

        free(roles);
    }
}

/* end of constructor & destructor */

/*
 * accessor
 */
/* name */
void ComponentBase::SetName(const OMX_STRING name)
{
    strncpy(this->name, name, OMX_MAX_STRINGNAME_SIZE);
    this->name[OMX_MAX_STRINGNAME_SIZE-1] = '\0';
}

const OMX_STRING ComponentBase::GetName(void)
{
    return name;
}

/* component module */
void ComponentBase::SetCModule(CModule *cmodule)
{
    this->cmodule = cmodule;
}

CModule *ComponentBase::GetCModule(void)
{
    return cmodule;
}

/* end of accessor */

/*
 * core methods & helpers
 */
/* roles */
OMX_ERRORTYPE ComponentBase::SetRolesOfComponent(OMX_U32 nr_roles,
                                                 const OMX_U8 **roles)
{
    OMX_U32 i;

    this->roles = (OMX_U8 **)malloc(sizeof(OMX_STRING) * nr_roles);
    if (!this->roles)
        return OMX_ErrorInsufficientResources;

    for (i = 0; i < nr_roles; i++) {
        this->roles[i] = (OMX_U8 *)malloc(OMX_MAX_STRINGNAME_SIZE);
        if (!this->roles[i]) {
            int j;

            for (j = (int )i-1; j >= 0; j--)
                free(this->roles[j]);
            free(this->roles);

            return OMX_ErrorInsufficientResources;
        }

        strncpy((OMX_STRING)&this->roles[i][0],
                (const OMX_STRING)&roles[i][0],
                OMX_MAX_STRINGNAME_SIZE);
    }

    this->nr_roles = nr_roles;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE ComponentBase::GetRolesOfComponent(OMX_U32 *nr_roles,
                                                 OMX_U8 **roles)
{
    OMX_U32 i;
    OMX_U32 this_nr_roles = this->nr_roles;

    if (!roles) {
        *nr_roles = this_nr_roles;
        return OMX_ErrorNone;
    }

    if (!nr_roles || (*nr_roles != this_nr_roles))
        return OMX_ErrorBadParameter;

    for (i = 0; i < this_nr_roles; i++) {
        if (!roles[i])
            break;

        if (roles && roles[i])
            strncpy((OMX_STRING)&roles[i][0],
                    (const OMX_STRING)&this->roles[i][0],
                    OMX_MAX_STRINGNAME_SIZE);
    }

    if (i != this_nr_roles)
        return OMX_ErrorBadParameter;

    *nr_roles = this_nr_roles;
    return OMX_ErrorNone;
}

bool ComponentBase::QueryHavingThisRole(const OMX_STRING role)
{
    OMX_U32 i;

    if (!roles || !role)
        return false;

    for (i = 0; i < nr_roles; i++) {
        if (!strcmp((OMX_STRING)&roles[i][0], role))
            return true;
    }

    return false;
}

/* GetHandle & FreeHandle */
OMX_ERRORTYPE ComponentBase::GetHandle(OMX_HANDLETYPE *pHandle,
                                       OMX_PTR pAppData,
                                       OMX_CALLBACKTYPE *pCallBacks)
{
    OMX_ERRORTYPE ret;

    if (handle)
        return OMX_ErrorUndefined;

    handle = (OMX_COMPONENTTYPE *)calloc(1, sizeof(*handle));
    if (!handle)
        return OMX_ErrorInsufficientResources;

    /* handle initialization */
    SetTypeHeader(handle, sizeof(*handle));
    handle->pComponentPrivate = static_cast<OMX_PTR>(this);
    handle->pApplicationPrivate = pAppData;

    /* virtual - see derived class */
    ret = InitComponent();
    if (ret != OMX_ErrorNone) {
        LOGE("failed to %s::InitComponent(), ret = 0x%08x\n",
             name, ret);
        goto free_handle;
    }

    /* connect handle's functions */
    handle->GetComponentVersion = GetComponentVersion;
    handle->SendCommand = SendCommand;
    handle->GetParameter = GetParameter;
    handle->SetParameter = SetParameter;
    handle->GetConfig = GetConfig;
    handle->SetConfig = SetConfig;
    handle->GetExtensionIndex = GetExtensionIndex;
    handle->GetState = GetState;
    handle->ComponentTunnelRequest = ComponentTunnelRequest;
    handle->UseBuffer = UseBuffer;
    handle->AllocateBuffer = AllocateBuffer;
    handle->FreeBuffer = FreeBuffer;
    handle->EmptyThisBuffer = EmptyThisBuffer;
    handle->FillThisBuffer = FillThisBuffer;
    handle->SetCallbacks = SetCallbacks;
    handle->ComponentDeInit = ComponentDeInit;
    handle->UseEGLImage = UseEGLImage;
    handle->ComponentRoleEnum = ComponentRoleEnum;

    appdata = pAppData;
    callbacks = pCallBacks;
    *pHandle = (OMX_HANDLETYPE *)handle;

    state = OMX_StateLoaded;
    return OMX_ErrorNone;

free_handle:
    free(this->handle);
    this->handle = NULL;

    return ret;
}

OMX_ERRORTYPE ComponentBase::FreeHandle(OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE ret;

    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    /* virtual - see derived class */
    ret = ExitComponent();
    if (ret != OMX_ErrorNone)
        return ret;

    free(handle);

    appdata = NULL;
    callbacks = NULL;

    state = OMX_StateUnloaded;
    return OMX_ErrorNone;
}

/* end of core methods & helpers */

/*
 * component methods & helpers
 */
OMX_ERRORTYPE ComponentBase::GetComponentVersion(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_OUT OMX_STRING pComponentName,
    OMX_OUT OMX_VERSIONTYPE* pComponentVersion,
    OMX_OUT OMX_VERSIONTYPE* pSpecVersion,
    OMX_OUT OMX_UUIDTYPE* pComponentUUID)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseGetComponentVersion(hComponent,
                                           pComponentName,
                                           pComponentVersion,
                                           pSpecVersion,
                                           pComponentUUID);
}

OMX_ERRORTYPE ComponentBase::CBaseGetComponentVersion(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_OUT OMX_STRING pComponentName,
    OMX_OUT OMX_VERSIONTYPE* pComponentVersion,
    OMX_OUT OMX_VERSIONTYPE* pSpecVersion,
    OMX_OUT OMX_UUIDTYPE* pComponentUUID)
{
    /*
     * Todo
     */

    return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE ComponentBase::SendCommand(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_COMMANDTYPE Cmd,
    OMX_IN  OMX_U32 nParam1,
    OMX_IN  OMX_PTR pCmdData)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseSendCommand(hComponent, Cmd, nParam1, pCmdData);
}

OMX_ERRORTYPE ComponentBase::CBaseSendCommand(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_COMMANDTYPE Cmd,
    OMX_IN  OMX_U32 nParam1,
    OMX_IN  OMX_PTR pCmdData)
{
    struct cmd_s *cmd;

    if (hComponent != handle)
        return OMX_ErrorInvalidComponent;

    /* basic error check */
    switch (Cmd) {
    case OMX_CommandStateSet:
        /*
         * Todo
         */
        break;
    case OMX_CommandFlush:
        /*
         * Todo
         */
        //break;
    case OMX_CommandPortDisable:
        /*
         * Todo
         */
        //break;
    case OMX_CommandPortEnable:
        /*
         * Todo
         */
        //break;
    case OMX_CommandMarkBuffer:
        /*
         * Todo
         */
        //break;
    default:
        LOGE("command %d not supported\n", Cmd);
        return OMX_ErrorUnsupportedIndex;
    }

    cmd = (struct cmd_s *)malloc(sizeof(*cmd));
    if (!cmd)
        return OMX_ErrorInsufficientResources;

    cmd->cmd = Cmd;
    cmd->param1 = nParam1;
    cmd->cmddata = pCmdData;

    return cmdwork->PushCmdQueue(cmd);
}

OMX_ERRORTYPE ComponentBase::GetParameter(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_INDEXTYPE nParamIndex,
    OMX_INOUT OMX_PTR pComponentParameterStructure)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseGetParameter(hComponent, nParamIndex,
                                    pComponentParameterStructure);
}

OMX_ERRORTYPE ComponentBase::CBaseGetParameter(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_INDEXTYPE nParamIndex,
    OMX_INOUT OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    switch (nParamIndex) {
    case OMX_IndexParamAudioInit:
    case OMX_IndexParamVideoInit:
    case OMX_IndexParamImageInit:
    case OMX_IndexParamOtherInit: {
        OMX_PORT_PARAM_TYPE *p =
            (OMX_PORT_PARAM_TYPE *)pComponentParameterStructure;

        memcpy(p, &portparam, sizeof(*p));
        break;
    }
    case OMX_IndexParamPortDefinition: {
        OMX_PARAM_PORTDEFINITIONTYPE *p =
            (OMX_PARAM_PORTDEFINITIONTYPE *)pComponentParameterStructure;
        OMX_U32 index = p->nPortIndex;
        PortBase *port = ports[index];

        memcpy(p, port->GetPortParam(), sizeof(*p));
        break;
    }
    case OMX_IndexParamAudioPortFormat: {
        OMX_AUDIO_PARAM_PORTFORMATTYPE *p =
            (OMX_AUDIO_PARAM_PORTFORMATTYPE *)pComponentParameterStructure;
        OMX_U32 index = p->nPortIndex;
        PortBase *port = ports[index];

        memcpy(p, port->GetAudioPortParam(), sizeof(*p));
        break;
    }
    case OMX_IndexParamCompBufferSupplier:
        /*
         * Todo
         */

        ret = OMX_ErrorUnsupportedIndex;
        break;
    default:
        ret = ComponentGetParameter(nParamIndex, pComponentParameterStructure);
    } /* switch */

    return ret;
}

OMX_ERRORTYPE ComponentBase::SetParameter(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_INDEXTYPE nIndex,
    OMX_IN  OMX_PTR pComponentParameterStructure)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseSetParameter(hComponent, nIndex,
                                    pComponentParameterStructure);
}

OMX_ERRORTYPE ComponentBase::CBaseSetParameter(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_INDEXTYPE nIndex,
    OMX_IN  OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    switch (nIndex) {
    case OMX_IndexParamAudioInit:
    case OMX_IndexParamVideoInit:
    case OMX_IndexParamImageInit:
    case OMX_IndexParamOtherInit: {
        OMX_PORT_PARAM_TYPE *p = (OMX_PORT_PARAM_TYPE *)
            pComponentParameterStructure;

        memcpy(&portparam, p, sizeof(*p));
        break;
    }
    case OMX_IndexParamPortDefinition: {
        OMX_PARAM_PORTDEFINITIONTYPE *p =
            (OMX_PARAM_PORTDEFINITIONTYPE *)pComponentParameterStructure;
        OMX_U32 index = p->nPortIndex;
        PortBase *port = ports[index];

        port->SetPortParam(p);
        break;
    }
    case OMX_IndexParamAudioPortFormat: {
        OMX_AUDIO_PARAM_PORTFORMATTYPE *p =
            (OMX_AUDIO_PARAM_PORTFORMATTYPE *)pComponentParameterStructure;
        OMX_U32 index = p->nPortIndex;
        PortBase *port = ports[index];

        port->SetAudioPortParam(p);
        break;
    }
    case OMX_IndexParamCompBufferSupplier:
        /*
         * Todo
         */

        ret = OMX_ErrorUnsupportedIndex;
        break;
    default:
        ret = ComponentSetParameter(nIndex, pComponentParameterStructure);
    } /* switch */

    return ret;
}

OMX_ERRORTYPE ComponentBase::GetConfig(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_INDEXTYPE nIndex,
    OMX_INOUT OMX_PTR pComponentConfigStructure)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseGetConfig(hComponent, nIndex,
                                 pComponentConfigStructure);
}

OMX_ERRORTYPE ComponentBase::CBaseGetConfig(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_INDEXTYPE nIndex,
    OMX_INOUT OMX_PTR pComponentConfigStructure)
{
    OMX_ERRORTYPE ret;

    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    switch (nIndex) {
    default:
        ret = ComponentGetConfig(nIndex, pComponentConfigStructure);
    }

    return ret;
}

OMX_ERRORTYPE ComponentBase::SetConfig(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_INDEXTYPE nIndex,
    OMX_IN  OMX_PTR pComponentConfigStructure)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseSetConfig(hComponent, nIndex,
                                 pComponentConfigStructure);
}

OMX_ERRORTYPE ComponentBase::CBaseSetConfig(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_INDEXTYPE nIndex,
    OMX_IN  OMX_PTR pComponentConfigStructure)
{
    OMX_ERRORTYPE ret;

    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    switch (nIndex) {
    default:
        ret = ComponentSetConfig(nIndex, pComponentConfigStructure);
    }

    return ret;
}

OMX_ERRORTYPE ComponentBase::GetExtensionIndex(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_STRING cParameterName,
    OMX_OUT OMX_INDEXTYPE* pIndexType)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseGetExtensionIndex(hComponent, cParameterName,
                                         pIndexType);
}

OMX_ERRORTYPE ComponentBase::CBaseGetExtensionIndex(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_STRING cParameterName,
    OMX_OUT OMX_INDEXTYPE* pIndexType)
{
    /*
     * Todo
     */

    return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE ComponentBase::GetState(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_OUT OMX_STATETYPE* pState)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseGetState(hComponent, pState);
}

OMX_ERRORTYPE ComponentBase::CBaseGetState(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_OUT OMX_STATETYPE* pState)
{
    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    *pState = state;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE ComponentBase::ComponentTunnelRequest(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_U32 nPort,
    OMX_IN  OMX_HANDLETYPE hTunneledComponent,
    OMX_IN  OMX_U32 nTunneledPort,
    OMX_INOUT  OMX_TUNNELSETUPTYPE* pTunnelSetup)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseComponentTunnelRequest(hComponent, nPort,
                                              hTunneledComponent,
                                              nTunneledPort, pTunnelSetup);
}

OMX_ERRORTYPE ComponentBase::CBaseComponentTunnelRequest(
    OMX_IN  OMX_HANDLETYPE hComp,
    OMX_IN  OMX_U32 nPort,
    OMX_IN  OMX_HANDLETYPE hTunneledComp,
    OMX_IN  OMX_U32 nTunneledPort,
    OMX_INOUT  OMX_TUNNELSETUPTYPE* pTunnelSetup)
{
    /*
     * Todo
     */

    return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE ComponentBase::UseBuffer(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE **ppBufferHdr,
    OMX_IN OMX_U32 nPortIndex,
    OMX_IN OMX_PTR pAppPrivate,
    OMX_IN OMX_U32 nSizeBytes,
    OMX_IN OMX_U8 *pBuffer)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseUseBuffer(hComponent, ppBufferHdr, nPortIndex,
                                 pAppPrivate, nSizeBytes, pBuffer);
}

OMX_ERRORTYPE ComponentBase::CBaseUseBuffer(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE **ppBufferHdr,
    OMX_IN OMX_U32 nPortIndex,
    OMX_IN OMX_PTR pAppPrivate,
    OMX_IN OMX_U32 nSizeBytes,
    OMX_IN OMX_U8 *pBuffer)
{
    PortBase *port = NULL;
    OMX_ERRORTYPE ret;

    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    if (ports)
        if (nPortIndex <= nr_ports-1)
            port = ports[nPortIndex];

    if (!port)
        return OMX_ErrorBadParameter;

    return port->UseBuffer(ppBufferHdr, nPortIndex, pAppPrivate, nSizeBytes,
                           pBuffer);
}

OMX_ERRORTYPE ComponentBase::AllocateBuffer(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE **ppBuffer,
    OMX_IN OMX_U32 nPortIndex,
    OMX_IN OMX_PTR pAppPrivate,
    OMX_IN OMX_U32 nSizeBytes)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseAllocateBuffer(hComponent, ppBuffer, nPortIndex,
                                      pAppPrivate, nSizeBytes);
}

OMX_ERRORTYPE ComponentBase::CBaseAllocateBuffer(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE **ppBuffer,
    OMX_IN OMX_U32 nPortIndex,
    OMX_IN OMX_PTR pAppPrivate,
    OMX_IN OMX_U32 nSizeBytes)
{
    PortBase *port = NULL;
    OMX_ERRORTYPE ret;

    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    if (ports)
        if (nPortIndex <= nr_ports)
            port = ports[nPortIndex];

    if (!port)
        return OMX_ErrorBadParameter;

    return port->AllocateBuffer(ppBuffer, nPortIndex, pAppPrivate, nSizeBytes);
}

OMX_ERRORTYPE ComponentBase::FreeBuffer(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_U32 nPortIndex,
    OMX_IN  OMX_BUFFERHEADERTYPE *pBuffer)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseFreeBuffer(hComponent, nPortIndex, pBuffer);
}

OMX_ERRORTYPE ComponentBase::CBaseFreeBuffer(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_U32 nPortIndex,
    OMX_IN  OMX_BUFFERHEADERTYPE *pBuffer)
{
    PortBase *port = NULL;
    OMX_ERRORTYPE ret;

    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    if (ports)
        if (nPortIndex <= nr_ports)
            port = ports[nPortIndex];

    if (!port)
        return OMX_ErrorBadParameter;

    return port->FreeBuffer(nPortIndex, pBuffer);
}

OMX_ERRORTYPE ComponentBase::EmptyThisBuffer(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseEmptyThisBuffer(hComponent, pBuffer);
}

OMX_ERRORTYPE ComponentBase::CBaseEmptyThisBuffer(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer)
{
    /*
     * Todo
     */

    return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE ComponentBase::FillThisBuffer(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseFillThisBuffer(hComponent, pBuffer);
}

OMX_ERRORTYPE ComponentBase::CBaseFillThisBuffer(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_BUFFERHEADERTYPE* pBuffer)
{
    /*
     * Todo
     */

    return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE ComponentBase::SetCallbacks(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_CALLBACKTYPE* pCallbacks,
    OMX_IN  OMX_PTR pAppData)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseSetCallbacks(hComponent, pCallbacks, pAppData);
}

OMX_ERRORTYPE ComponentBase::CBaseSetCallbacks(
    OMX_IN  OMX_HANDLETYPE hComponent,
    OMX_IN  OMX_CALLBACKTYPE *pCallbacks,
    OMX_IN  OMX_PTR pAppData)
{
    if (hComponent != handle)
        return OMX_ErrorBadParameter;

    appdata = pAppData;
    callbacks = pCallbacks;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE ComponentBase::ComponentDeInit(
    OMX_IN  OMX_HANDLETYPE hComponent)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseComponentDeInit(hComponent);
}

OMX_ERRORTYPE ComponentBase::CBaseComponentDeInit(
    OMX_IN  OMX_HANDLETYPE hComponent)
{
    /*
     * Todo
     */

    return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE ComponentBase::UseEGLImage(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
    OMX_IN OMX_U32 nPortIndex,
    OMX_IN OMX_PTR pAppPrivate,
    OMX_IN void* eglImage)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseUseEGLImage(hComponent, ppBufferHdr, nPortIndex,
                                   pAppPrivate, eglImage);
}

OMX_ERRORTYPE ComponentBase::CBaseUseEGLImage(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE** ppBufferHdr,
    OMX_IN OMX_U32 nPortIndex,
    OMX_IN OMX_PTR pAppPrivate,
    OMX_IN void* eglImage)
{
    /*
     * Todo
     */

    return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE ComponentBase::ComponentRoleEnum(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_OUT OMX_U8 *cRole,
    OMX_IN OMX_U32 nIndex)
{
    ComponentBase *cbase;

    if (!hComponent)
        return OMX_ErrorBadParameter;

    cbase = static_cast<ComponentBase *>
        (((OMX_COMPONENTTYPE *)hComponent)->pComponentPrivate);
    if (!cbase)
        return OMX_ErrorBadParameter;

    return cbase->CBaseComponentRoleEnum(hComponent, cRole, nIndex);
}

OMX_ERRORTYPE ComponentBase::CBaseComponentRoleEnum(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_OUT OMX_U8 *cRole,
    OMX_IN OMX_U32 nIndex)
{
    if (hComponent != (OMX_HANDLETYPE *)this->handle)
        return OMX_ErrorBadParameter;

    if (nIndex > nr_roles)
        return OMX_ErrorBadParameter;

    strncpy((char *)cRole, (const char *)roles[nIndex],
            OMX_MAX_STRINGNAME_SIZE);
    return OMX_ErrorNone;
}

/* implement CmdHandlerInterface */
void ComponentBase::CmdHandler(struct cmd_s *cmd)
{
    switch (cmd->cmd) {
    case OMX_CommandStateSet: {
        OMX_STATETYPE transition = (OMX_STATETYPE)cmd->param1;

        TransState(transition);
        break;
    }
    case OMX_CommandFlush:
        /*
         * Todo
         */
        break;
    case OMX_CommandPortDisable:
        /*
         * Todo
         */
        break;
    case OMX_CommandPortEnable:
        /*
         * Todo
         */
        break;
    case OMX_CommandMarkBuffer:
        /*
         * Todo
         */
        break;
    } /* switch */
}

/*
 * SendCommand:OMX_CommandStateSet
 * called in CmdHandler or called in other parts of component for reporting
 * internal error (OMX_StateInvalid).
 */
/*
 * Todo
 *   Resource Management (OMX_StateWaitForResources)
 *   for now, we never notify OMX_ErrorInsufficientResources,
 *   so IL client doesn't try to set component' state OMX_StateWaitForResources
 */
static const char *state_name[OMX_StateWaitForResources + 1] = {
    "OMX_StateInvalid",
    "OMX_StateLoaded",
    "OMX_StateIdle",
    "OMX_StateExecuting",
    "OMX_StatePause",
    "OMX_StateWaitForResources",
};

static inline const char *GetStateName(OMX_STATETYPE state)
{
    if (state > OMX_StateWaitForResources)
        return "UnKnown";

    return state_name[state];
}

void ComponentBase::TransState(OMX_STATETYPE transition)
{
    OMX_STATETYPE current = this->state;
    OMX_EVENTTYPE event;
    OMX_U32 data1;
    OMX_ERRORTYPE ret;

    LOGD("current state = %s, transition state = %s\n",
         GetStateName(current), GetStateName(transition));

    /* same state */
    if (current == transition) {
        ret = OMX_ErrorSameState;
        goto notify_event;
    }

    /* invalid state */
    if (current == OMX_StateInvalid) {
        ret = OMX_ErrorInvalidState;
        goto notify_event;
    }

    if (transition == OMX_StateLoaded)
        ret = TransStateToLoaded(current);
    else if (transition == OMX_StateIdle)
        ret = TransStateToIdle(current);
    else if (transition == OMX_StateExecuting)
        ret = TransStateToExecuting(current);
    else if (transition == OMX_StatePause)
        ret = TransStateToPause(current);
    else if (transition == OMX_StateInvalid)
        ret = TransStateToInvalid(current);
    else if (transition == OMX_StateWaitForResources)
        ret = TransStateToWaitForResources(current);
    else
        ret = OMX_ErrorIncorrectStateTransition;

notify_event:
    if (ret == OMX_ErrorNone) {
        event = OMX_EventCmdComplete;
        data1 = transition;

        state = transition;
        LOGD("transition from %s to %s completed\n",
             GetStateName(current), GetStateName(transition));
    }
    else {
        event = OMX_EventError;
        data1 = ret;

        if (transition == OMX_StateInvalid) {
            state = transition;
            LOGD("transition from %s to %s completed\n",
                 GetStateName(current), GetStateName(transition));
        }
    }

    callbacks->EventHandler(handle, appdata, event, data1, 0, NULL);

    /* WaitForResources workaround */
    if (ret == OMX_ErrorNone && transition == OMX_StateWaitForResources)
        callbacks->EventHandler(handle, appdata,
                                OMX_EventResourcesAcquired, 0, 0, NULL);
}

inline OMX_ERRORTYPE ComponentBase::TransStateToLoaded(OMX_STATETYPE current)
{
    OMX_ERRORTYPE ret;

    if (current == OMX_StateIdle) {
        /*
         * Todo
         *   1. waits for completion of deallocation on each port
         *      wokeup by FreeBuffer()
         *   2. deinitialize buffer process work
         *   3. deinitialize component's internal processor
         *      (ex. deinitialize sw/hw codec)
         */
        OMX_U32 i;

        for (i = 0; i < nr_ports; i++)
            ports[i]->WaitPortBufferCompletion();

        ret = OMX_ErrorNone;
    }
    else if (current == OMX_StateWaitForResources) {
        LOGE("state transition's requested from WaitForResources to "
             "Loaded\n");

        /*
         * from WaitForResources to Loaded considered from Loaded to Loaded.
         * do nothing
         */

        ret = OMX_ErrorNone;
    }
    else
        ret = OMX_ErrorIncorrectStateOperation;

    return ret;
}

inline OMX_ERRORTYPE ComponentBase::TransStateToIdle(OMX_STATETYPE current)
{
    OMX_ERRORTYPE ret;

    if (current == OMX_StateLoaded) {
        /*
         * Todo
         *   1. waits for completion of allocation on each port.
         *      wokeup by Allocate/UseBuffer()
         *   2. initialize buffer process work.
         *   3. initialize component's internal processor.
         *      (ex. initialize sw/hw codec)
         */
        OMX_U32 i;

        for (i = 0; i < nr_ports; i++)
            ports[i]->WaitPortBufferCompletion();

        ret = OMX_ErrorNone;
    }
    else if (current == OMX_StateExecuting) {
        /*
         * Todo
         *   1. returns all buffers to thier suppliers.
         *      call Fill/EmptyThisBuffer() for all ports
         *   2. stop buffer process work
         *   3. stop component's internal processor
         */
        ret = OMX_ErrorNone;
    }
    else if (current == OMX_StatePause) {

        /* same as Executing to Idle */

        ret = OMX_ErrorNone;
    }
    else if (current == OMX_StateWaitForResources) {
        LOGE("state transition's requested from WaitForResources to Idle\n");

        /* same as Loaded to Idle BUT DO NOTHING for now */

        ret = OMX_ErrorNone;
    }
    else
        ret = OMX_ErrorIncorrectStateOperation;

    return ret;
}

inline OMX_ERRORTYPE
ComponentBase::TransStateToExecuting(OMX_STATETYPE current)
{
    OMX_ERRORTYPE ret;

    if (current == OMX_StateIdle) {
        /*
         * Todo
         *   1. start component's internal processor
         *   2. start processing buffers on each port
         */
        ret = OMX_ErrorNone;
    }
    else if (current == OMX_StatePause) {
        /*
         * Todo
         *   1. resume buffer process woraek
         *   2. resume component's internal processor
         */
        ret = OMX_ErrorNone;
    }
    else
        ret = OMX_ErrorIncorrectStateOperation;

    return ret;
}

inline OMX_ERRORTYPE ComponentBase::TransStateToPause(OMX_STATETYPE current)
{
    OMX_ERRORTYPE ret;

    if (current == OMX_StateIdle) {
        /*
         * same as Idle to Executing,
         * except for not starting buffer processing and internal processor
         */
        ret = OMX_ErrorNone;
    }
    else if (current == OMX_StateExecuting) {
        /*
         * Todo
         *   1. pause buffer process work
         *   2. pause component's internal processor
         */
        ret = OMX_ErrorNone;
    }
    else
        ret = OMX_ErrorIncorrectStateOperation;

    return ret;
}

inline OMX_ERRORTYPE ComponentBase::TransStateToInvalid(OMX_STATETYPE current)
{
    OMX_ERRORTYPE ret = OMX_ErrorInvalidState;

    /*
     * Todo
     *   graceful escape
     */

    return ret;
}

inline OMX_ERRORTYPE
ComponentBase::TransStateToWaitForResources(OMX_STATETYPE current)
{
    OMX_ERRORTYPE ret;

    if (current == OMX_StateLoaded) {
        LOGE("state transition's requested from Loaded to WaitForResources\n");
        ret = OMX_ErrorNone;
    }
    else
        ret = OMX_ErrorIncorrectStateOperation;

    return ret;
}

/* end of component methods & helpers */

/*
 * omx header manipuation
 */
void ComponentBase::SetTypeHeader(OMX_PTR type, OMX_U32 size)
{
    OMX_U32 *nsize;
    OMX_VERSIONTYPE *nversion;

    if (!type)
        return;

    nsize = (OMX_U32 *)type;
    nversion = (OMX_VERSIONTYPE *)((OMX_U8 *)type + sizeof(OMX_U32));

    *nsize = size;
    nversion->nVersion = OMX_SPEC_VERSION;
}

OMX_ERRORTYPE ComponentBase::CheckTypeHeader(OMX_PTR type, OMX_U32 size)
{
    OMX_U32 *nsize;
    OMX_VERSIONTYPE *nversion;

    if (!type)
        return OMX_ErrorBadParameter;

    nsize = (OMX_U32 *)type;
    nversion = (OMX_VERSIONTYPE *)((OMX_U8 *)type + sizeof(OMX_U32));

    if (*nsize != size)
        return OMX_ErrorBadParameter;

    if (nversion->nVersion != OMX_SPEC_VERSION)
        return OMX_ErrorVersionMismatch;

    return OMX_ErrorNone;
}

/* end of ComponentBase */
