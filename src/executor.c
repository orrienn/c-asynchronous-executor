#include "executor.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "future.h"
#include "mio.h"
#include "waker.h"

/**
 * @brief Structure to represent the current-thread executor.
 */
struct Executor 
{
    Future** queue;
    size_t queue_size;
    size_t queue_count;
    Mio* mio;
};

Executor* executor_create(size_t max_queue_size) 
{
    Executor* executor = malloc(sizeof(Executor));
    if (!executor) return NULL;
    
    executor->queue = malloc(sizeof(Future*) * max_queue_size);
    if (!executor->queue) 
    {
        free(executor);
        return NULL;
    }
    
    executor->queue_size = max_queue_size;
    executor->queue_count = 0;
    executor->mio = mio_create(executor);
    if(!executor->mio) 
    {
        free(executor->queue);
        free(executor);
        return NULL;
    }
    
    return executor;
}

void waker_wake(Waker* waker) 
{
    Executor* executor = (Executor*)waker->executor;
    Future* future = waker->future;
    
    if(!executor || !future) return;
    
    if(executor->queue_count < executor->queue_size) 
    {
        executor->queue[executor->queue_count++] = future;
    }
}

void executor_spawn(Executor* executor, Future* fut) 
{
    if(executor->queue_count < executor->queue_size) 
    {
        executor->queue[executor->queue_count++] = fut;
        fut->is_active = true;
    }
}


void executor_run(Executor* executor) 
{
    while (executor->queue_count > 0) 
    {
        for (size_t i = 0; i < executor->queue_count; i++) 
        {
            Future* fut = executor->queue[i];
            if (!fut) continue;

            FutureState state = fut->progress(fut, executor->mio, (Waker){executor, fut});
            if(state == FUTURE_COMPLETED || state == FUTURE_FAILURE) 
            {
                fut->is_active = false;
                executor->queue[i] = executor->queue[--executor->queue_count];
            }
        }

        if (executor->queue_count == 0) break;
        mio_poll(executor->mio);
    }
}

void executor_destroy(Executor* executor) 
{
    if (!executor) return;
    mio_destroy(executor->mio);
    free(executor->queue);
    free(executor);
}