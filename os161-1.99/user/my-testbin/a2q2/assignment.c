#include "assignment.h"
#include <stdio.h>

void
consume_enter(struct resource *resource)
{
    while(!(resource->num_consumers+1 <= resource->num_producers * resource->ratio)) {
        printf("bro...\n");
        pthread_cond_wait(&resource->cond, &resource->mutex);
    }
    resource->num_consumers += 1;
}

void
consume_exit(struct resource *resource)
{
    resource->num_consumers -= 1;
    pthread_cond_signal(&resource->cond);
}

void
produce_enter(struct resource *resource)
{
    resource->num_producers += 1;
    pthread_cond_signal(&resource->cond);
}

void
produce_exit(struct resource *resource)
{
    printf("ur mom is a hoe\n");
    printf("ur mom gay: %ld\n", resource->num_consumers);
    while(!(resource->num_consumers <= (resource->num_producers-1) * resource->ratio)) {
        pthread_cond_wait(&resource->cond, &resource->mutex);
    }
    resource->num_producers -= 1;
    printf("prod exit\n");
}


