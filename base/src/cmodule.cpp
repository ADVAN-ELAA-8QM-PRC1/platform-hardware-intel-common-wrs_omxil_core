/*
 * Copyright (C) 2009 Wind River Systems.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OMX_Core.h>

#include <cmodule.h>

#define LOG_TAG "cmodule"
#include <log.h>

CModule::CModule()
{
    memset(name, 0, OMX_MAX_STRINGNAME_SIZE);

    module = NULL;
    privdata = NULL;
    init = NULL;
    exit = NULL;
}

CModule::CModule(const OMX_STRING name)
{
    CModule();
    SetName(name);
}

CModule::~CModule()
{
    Unload();
}

OMX_ERRORTYPE CModule::Load()
{
    OMX_ERRORTYPE init_ret;

    if (module)
        return OMX_ErrorNotReady;

    module = module_open(name, MODULE_NOW);
    if (!module) {
        LOGE("module not founded (%s)\n", name);
        return OMX_ErrorComponentNotFound;
    }
    LOGV("module founded (%s)\n", name);

    init = (cmodule_init_t)module_symbol(module, "omx_component_module_init");
    if (!init) {
        LOGE("module init symbol not founded (%s)\n", name);
        module_close(module);
        return OMX_ErrorInvalidComponent;
    }

    exit = (cmodule_exit_t)module_symbol(module, "omx_component_module_exit");
    if (!exit) {
        LOGE("module exit symbol not founded (%s)\n", name);
        module_close(module);
        return OMX_ErrorInvalidComponent;
    }

    init_ret = init(this);
    if (init_ret != OMX_ErrorNone) {
        LOGE("module init returned (%d)\n", init_ret);
        module_close(module);
        module = NULL;
        return init_ret;
    }

    LOGV("module %s successfully loaded\n", name);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE CModule::Unload(void)
{
    OMX_ERRORTYPE exit_ret;

    if (!module)
        return OMX_ErrorInvalidComponent;

    exit_ret = exit(this);
    if (exit_ret) {
        LOGE("module exit returned (%d)\n", exit_ret);
        return exit_ret;
    }

    module_close(module);
    module = NULL;

    LOGV("module %s successfully unloaded\n", name);
    return OMX_ErrorNone;
}

void CModule::SetName(const OMX_STRING name)
{
    strncpy(this->name, name, OMX_MAX_STRINGNAME_SIZE);
    name[OMX_MAX_STRINGNAME_SIZE-1] = '\0';
}

const OMX_STRING CModule::GetName(void)
{
    return name;
}

void CModule::SetPrivData(OMX_PTR privdata)
{
    this->privdata = privdata;
}

OMX_PTR CModule::GetPrivData(void)
{
    return privdata;
}
