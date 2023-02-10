#include "assignment.h"

void
consume_enter(struct resource *resource)
{
    //don't try to lock mutex that is already locked in orderme.c for some dumbass reason
    pthread_mutex_lock(&resource->mutex);
    while(!(resource->num_consumers+1 <= resource->num_producers*resource->ratio)) {
        //wait for cond to become true
        pthread_cond_wait(&resource->cond, &resource->mutex);
    }
    resource->num_consumers += 1;
}

void
consume_exit(struct resource *resource)
{
    //again, the dumbass mutex is locked when this function is called
    resource->num_consumers -= 1;
    pthread_cond_signal(&resource->cond);
    pthread_mutex_unlock(&resource->mutex);
}

void
produce_enter(struct resource *resource)
{
    // FILL ME IN
    pthread_mutex_lock(&resource->mutex);
    resource->num_producers+=1;
    pthread_cond_signal(&resource->cond);
}

void
produce_exit(struct resource *resource)
{
    // FILL ME IN
    while(!(resource->num_consumers <= (resource->num_producers-1)*resource->ratio)) {
        pthread_cond_wait(&resource->cond, &resource->mutex);
    }
    resource->num_producers -=1;
    pthread_mutex_unlock(&resource->mutex);
}


