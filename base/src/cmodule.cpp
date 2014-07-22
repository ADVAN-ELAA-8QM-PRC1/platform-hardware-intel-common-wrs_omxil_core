/*
 * Copyright (c) 2009 Wind River Systems, Inc.
 *
 * The right to copy, distribute, modify, or otherwise make use
 * of this software may be licensed only pursuant to the terms
 * of an applicable Wind River license agreement.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OMX_Core.h>

#include <cmodule.h>
#include <componentbase.h>

#define LOG_TAG "cmodule"
#include <log.h>

/*
 * constructor / deconstructor
 */
CModule::CModule(const OMX_STRING lname)
{
    module = NULL;

    instantiate = NULL;
    query_name = NULL;
    query_roles = NULL;

    roles = NULL;
    nr_roles = 0;

    memset(cname, 0, OMX_MAX_STRINGNAME_SIZE);

    memset(this->lname, 0, OMX_MAX_STRINGNAME_SIZE);
    strncpy(this->lname, lname, OMX_MAX_STRINGNAME_SIZE);
    this->lname[OMX_MAX_STRINGNAME_SIZE-1] = '\0';
}

CModule::~CModule()
{
    while (Unload()) ;

    if (roles) {
        if (roles[0])
            free(roles[0]);
        free(roles);
    }
}

/* end of constructor / deconstructor */

/*
 * library loading / unloading
 */
OMX_ERRORTYPE CModule::Load()
{
    struct module *m;

    m = module_open(lname, MODULE_NOW);
    if (!m) {
        LOGE("module not founded (%s)\n", lname);
        return OMX_ErrorComponentNotFound;
    }
    LOGV("module founded (%s)\n", lname);

    if (!module) {
        instantiate = (cmodule_instantiate_t)
            module_symbol(m, "omx_component_module_instantiate");
        if (!instantiate) {
            LOGE("module instantiate() symbol not founded (%s)\n", lname);
            module_close(m);
            return OMX_ErrorInvalidComponent;
        }

        query_name = (cmodule_query_name_t)
            module_symbol(m, "omx_component_module_query_name");
        if (!query_name) {
            LOGE("module query_name() symbol not founded (%s)\n", lname);

            instantiate = NULL;
            module_close(m);
            return OMX_ErrorInvalidComponent;
        }

        query_roles = (cmodule_query_roles_t)
            module_symbol(m, "omx_component_module_query_roles");
        if (!query_roles) {
            LOGE("module query_roles() symbol not founded (%s)\n", lname);

            query_name = NULL;
            instantiate = NULL;
            module_close(m);
            return OMX_ErrorInvalidComponent;
        }

        module = m;
        LOGV("module %s successfully loaded\n", lname);
    }
    else {
        if (module != m) {
            LOGE("module fatal error, module != m (%p != %p)\n", module, m);
            module_close(m);
            return OMX_ErrorUndefined;
        }
    }

    return OMX_ErrorNone;
}

OMX_U32 CModule::Unload(void)
{
    OMX_U32 ref_count;

    if (!module)
        return 0;

    ref_count = module_close(module);
    if (!ref_count) {
        module = NULL;
        instantiate = NULL;
        query_name = NULL;
        query_roles = NULL;

        LOGV("module %s successfully unloaded\n", lname);
    }

    return ref_count;
}

/* end of library loading / unloading */

/*
 * accessor
 */
const OMX_STRING CModule::GetLibraryName(void)
{
    return lname;
}

const OMX_STRING CModule::GetComponentName(void)
{
    return cname;
}

OMX_ERRORTYPE CModule::GetComponentRoles(OMX_U32 *nr_roles, OMX_U8 **roles)
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

bool CModule::QueryHavingThisRole(const OMX_STRING role)
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

/* end of accessor */

/*
 * library symbol method and helpers
 */
/* call instance / query_name /query_roles */
OMX_ERRORTYPE CModule::QueryComponentName(void)
{
    OMX_ERRORTYPE ret = OMX_ErrorUndefined;

    if (query_name)
        ret = query_name(&cname[0], OMX_MAX_STRINGNAME_SIZE);

    cname[OMX_MAX_STRINGNAME_SIZE-1] = '\0';
    return ret;
}

OMX_ERRORTYPE CModule::QueryComponentName(OMX_STRING cname, OMX_U32 len)
{
    OMX_ERRORTYPE ret = OMX_ErrorUndefined;

    if (query_name)
        ret = query_name(cname, len);

    cname[len-1] = '\0';
    return ret;
}

OMX_ERRORTYPE CModule::QueryComponentRoles(void)
{
    OMX_ERRORTYPE ret;

    if (!query_roles)
        return OMX_ErrorUndefined;

    if (nr_roles && roles)
        return OMX_ErrorNone;

    roles = NULL;
    ret = query_roles(&nr_roles, roles);
    if (ret != OMX_ErrorNone) {
        nr_roles = 0;
        roles = NULL;
    }

    roles = (OMX_U8 **)malloc(sizeof(OMX_STRING) * nr_roles);
    if (roles) {
        roles[0] = (OMX_U8 *)malloc(OMX_MAX_STRINGNAME_SIZE * nr_roles);
        if (roles[0]) {
            OMX_U32 i;

            for (i = 0; i < nr_roles-1; i++)
                roles[i+1] = roles[i] + OMX_MAX_STRINGNAME_SIZE;
        }
        else {
            free(roles);
            roles = NULL;
            nr_roles = 0;
            return OMX_ErrorInsufficientResources;
        }
    }
    else  {
        nr_roles = 0;
        roles = NULL;
        return OMX_ErrorInsufficientResources;
    }

    ret = query_roles(&nr_roles, roles);
    if (ret != OMX_ErrorNone) {
        nr_roles = 0;
        free(roles[0]);
        free(roles);
        roles = NULL;
        return ret;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE CModule::QueryComponentRoles(OMX_U32 *nr_roles, OMX_U8 **roles)
{
    OMX_ERRORTYPE ret = OMX_ErrorUndefined;

    if (query_roles)
        ret = query_roles(nr_roles, roles);

    return ret;
}

OMX_ERRORTYPE CModule::InstantiateComponent(ComponentBase **instance)
{
    OMX_ERRORTYPE ret = OMX_ErrorUndefined;

    if (!instance || *instance)
        return OMX_ErrorBadParameter;
    *instance = NULL;

    if (instantiate) {
        ComponentBase *cbase;

        ret = instantiate((void **)instance);
        if (ret != OMX_ErrorNone) {
            instance = NULL;
            return ret;
        }
        cbase = *instance;

        cbase->SetCModule(this);
        cbase->SetName(cname);
        ret = cbase->SetRolesOfComponent(nr_roles, (const OMX_U8 **)roles);
        if (ret != OMX_ErrorNone) {
            delete cbase;
            *instance = NULL;
            return ret;
        }
    }

    return ret;
}

/* end of library symbol method and helpers */
