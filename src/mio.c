#include "mio.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "debug.h"
#include "executor.h"
#include "waker.h"

// Maximum number of events to handle per epoll_wait call.
#define MAX_EVENTS 64

typedef struct MioRegistration 
{
    int fd;
    Waker* waker;
    struct MioRegistration* next;
} MioRegistration;

struct Mio 
{
    int epoll_fd;
    Executor* executor;
    MioRegistration* registrations;
};


Mio* mio_create(Executor* executor) 
{
    Mio* mio = malloc(sizeof(Mio));
    if(!mio) 
    {
        debug("Failed to allocate Mio instance");
        return NULL;
    }

    mio->epoll_fd = epoll_create1(0);
    if(mio->epoll_fd == -1) 
    {
        debug("Failed to create epoll instance: %s", strerror(errno));
        free(mio);
        return NULL;
    }

    mio->executor = executor;
    mio->registrations = NULL;
    return mio;
}

void mio_destroy(Mio* mio) 
{
    if (!mio) return;
    close(mio->epoll_fd);
    MioRegistration* curr = mio->registrations;
    while(curr) 
    {
        MioRegistration* next = curr->next;
        free(curr->waker);
        free(curr);
        curr = next;
    }
    free(mio);
}

static void mio_update_registration(Mio* mio, int fd, Waker* new_waker) 
{
    MioRegistration* curr = mio->registrations;
    while(curr) 
    {
        if(curr->fd == fd) 
        {
            free(curr->waker);
            curr->waker = new_waker;
            return;
        }
        curr = curr->next;
    }
    MioRegistration* reg = malloc(sizeof(MioRegistration));
    if(!reg) 
    {
        debug("Failed to allocate MioRegistration");
        return;
    }
    reg->fd = fd;
    reg->waker = new_waker;
    reg->next = mio->registrations;
    mio->registrations = reg;
}


int mio_register(Mio* mio, int fd, uint32_t events, Waker waker) 
{
    Waker* new_waker = malloc(sizeof(Waker));
    if(!new_waker) 
    {
        debug("Failed to allocate memory for Waker");
        return -1;
    }

    memcpy(new_waker, &waker, sizeof(Waker));

    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.ptr = new_waker;

    int ret = epoll_ctl(mio->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    if(ret == -1) 
    {
        if(errno == EEXIST) 
        {
            ret = epoll_ctl(mio->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
            if(ret == -1) 
            {
                debug("Failed to modify fd %d with epoll: %s", fd, strerror(errno));
                free(new_waker);
                return -1;
            }
        } 
        else 
        {
            debug("Failed to register fd %d with epoll: %s", fd, strerror(errno));
            free(new_waker);
            return -1;
        }
    }
    mio_update_registration(mio, fd, new_waker);
    return 0;
}

int mio_unregister(Mio* mio, int fd) 
{
    debug("Unregistering (from Mio = %p) fd = %d\n", mio, fd);
    if(epoll_ctl(mio->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) 
    {
        debug("Failed to unregister fd %d from epoll: %s", fd, strerror(errno));
        return -1;
    }

    MioRegistration* prev = NULL;
    MioRegistration* curr = mio->registrations;
    while(curr) 
    {
        if(curr->fd == fd) 
        {
            if(prev)
                prev->next = curr->next;
            else
                mio->registrations = curr->next;
            free(curr->waker);
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;
}

void mio_poll(Mio* mio)
{
    debug("Mio (%p) polling\n", mio);

    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(mio->epoll_fd, events, MAX_EVENTS, -1);

    if(nfds == -1) 
    {
        debug("epoll_wait failed: %s", strerror(errno));
        return;
    }

    for(int i = 0; i < nfds; i++) 
    {
        Waker* waker = (Waker*)events[i].data.ptr;
        if(waker)
            waker_wake(waker);
    }
}
