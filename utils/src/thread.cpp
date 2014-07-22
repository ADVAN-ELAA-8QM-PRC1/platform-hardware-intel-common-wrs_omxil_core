/*
 * Copyright (C) 2009 Wind River Systems
 *      Author: Ho-Eun Ryu <ho-eun.ryu@windriver.com>
 */

#include <pthread.h>
#include <thread.h>

Thread::Thread()
{
    r = NULL;
    created = false;
}

Thread::Thread(RunnableInterface *r)
{
    this->r = r;
}

Thread::~Thread()
{
    Join();
}

int Thread::Start(void)
{
    int ret = 0;

    if (!created) {
        ret = pthread_create(&id, NULL, Instance, this);
        if (!ret)
            created = true;
    }

    return ret;
}

int Thread::Join(void)
{
    int ret = 0;

    if (created) {
        ret = pthread_join(id, NULL);
        created = false;
    }

    return ret;
}

void *Thread::Instance(void *p)
{
    Thread *t = static_cast<Thread *>(p);

    t->Run();

    return NULL;
}

void Thread::Run(void)
{
    if (r)
        r->Run();
    else
        return;
}
