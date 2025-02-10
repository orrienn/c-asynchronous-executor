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

struct Mio 
{
    int epoll_fd;
    Executor* executor;
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
    return mio;
}

void mio_destroy(Mio* mio) 
{
    if (!mio) return;
    close(mio->epoll_fd);
    free(mio);
}


int mio_register(Mio* mio, int fd, uint32_t events, Waker waker) 
{
    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.ptr = malloc(sizeof(Waker));
    if(!ev.data.ptr) 
    {
        debug("Failed to allocate memory for Waker");
        return -1;
    }

    memcpy(ev.data.ptr, &waker, sizeof(Waker));

    if(epoll_ctl(mio->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) 
    {
        debug("Failed to register fd %d with epoll: %s", fd, strerror(errno));
        free(ev.data.ptr);
        return -1;
    }

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
        {
            waker_wake(waker);
        }
    }
}
