/* map.c
 * ----------------------------------------------------------
 *  CS350
 *  Assignment 1
 *  Question 1
 *
 *  Purpose:  Gain experience with threads and basic
 *  synchronization.
 *
 *  YOU MAY ADD WHATEVER YOU LIKE TO THIS FILE.
 *  YOU CANNOT CHANGE THE SIGNATURE OF MultithreadedWordCount.
 * ----------------------------------------------------------
 */
#include "data.h"

#include <stdlib.h>
#include <string.h>

int globalCount = 0;

struct thread_data {
  struct Library * lib;
  char * word;
  int a;
  int b;
  int sum;
}; 

void* count(void * arg);

/* --------------------------------------------------------------------
 * MultithreadedWordCount
 * --------------------------------------------------------------------
 * Takes a Library of articles containing words and a word.
 * Returns the total number of times that the word appears in the
 * Library.
 *
 * For example, "There the thesis sits on the theatre bench.", contains
 * 2 occurences of the word "the".
 * --------------------------------------------------------------------
 */

size_t MultithreadedWordCount( struct  Library * lib, char * word, int NUMTHREADS)
{
  int ans = 0;
  printf("Parallelizing with %d threads...\n",NUMTHREADS);

  if(NUMTHREADS > lib->numArticles) {
    NUMTHREADS = lib->numArticles;
  }
  printf("threads: %d\n", NUMTHREADS);
  pthread_t *threads = malloc(NUMTHREADS*sizeof(pthread_t));
  struct thread_data *allData = malloc(NUMTHREADS*sizeof(struct thread_data));
  int div = (lib->numArticles)/NUMTHREADS;
  int cnt = 0;

  for(int i=0; i<NUMTHREADS; i++) {
    struct thread_data data;
    data.a = cnt;
    data.b = cnt+div;
    data.lib = lib;
    data.word = word;
    if(i==NUMTHREADS-1) {
      data.b = lib->numArticles;
    }
    allData[i] = data;
    pthread_create(&threads[i], NULL, count, (void*) &allData[i]);
    cnt+=div;
  }

  for(int i = 0; i<NUMTHREADS; i++) {
    pthread_join(threads[i], NULL);
  }

  for(int i = 0; i<NUMTHREADS; i++) {
    ans += allData[i].sum;
  }

    /* XXX FILLMEIN
     * Also feel free to remove the printf statement
     * to improve time */
    free(threads);
    free(allData);
    return ans;
}

void* count(void * arg)
{
    struct thread_data *data = (struct thread_data *) arg;
    struct Library * lib = data->lib;
    char * word = data->word;
    int a = data->a;
    int b = data->b;
    data->sum = 0;
    if(b>(int) lib->numArticles) {b = lib->numArticles;}
    //printf("b: %d\n", b);
    for (int i = a; i < b; i ++)
    {
      struct Article * art = lib->articles[i];
      for (unsigned int j = 0; j < art->numWords; j++)
      {
        // Get the length of the function.
        size_t len = strnlen( art->words[j], MAXWORDSIZE );
        if ( !strncmp( art->words[j], word, len ) )
        {
          data->sum += 1;
        }
      }
    }
    return NULL;
}
