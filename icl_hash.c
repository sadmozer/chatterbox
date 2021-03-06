/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */
/**
  * @file icl_hash.c
  *
  * @brief Dependency free hash table implementation.
  *
  * This simple hash table implementation should be easy to drop into
  * any other peice of code, it does not depend on anything else :-)
  *
  * @author Jakub Kurzak
  */
 /* $Id: icl_hash.c 2838 2011-11-22 04:25:02Z mfaverge $ */
 /* $UTK_Copyright: $ */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <limits.h>
#include <stdarg.h>

#include "message.h"
#include "icl_hash.h"
#include "connections.h"
#include "config.h"

#define BITS_IN_int     ( sizeof(int) * CHAR_BIT )
#define THREE_QUARTERS  ((int) ((BITS_IN_int * 3) / 4))
#define ONE_EIGHTH      ((int) (BITS_IN_int / 8))
#define HIGH_BITS       ( ~((unsigned int)(~0) >> ONE_EIGHTH ))

/**
 * A simple string hash.
 *
 * An adaptation of Peter Weinberger's (PJW) generic hashing
 * algorithm based on Allen Holub's version. Accepts a pointer
 * to a datum to be hashed and returns an unsigned integer.
 * From: Keith Seymour's proxy library code
 *
 * @param datum -- the string to be hashed
 *
 * @returns the hash index
 */
static unsigned int hash_pjw(void* key)
{
    char *datum = (char *)key;
    unsigned int hash_value, i;

    if(!datum) return 0;

    for (hash_value = 0; *datum; ++datum) {
        hash_value = (hash_value << ONE_EIGHTH) + *datum;
        if ((i = hash_value & HIGH_BITS) != 0)
            hash_value = (hash_value ^ (i >> THREE_QUARTERS)) & ~HIGH_BITS;
    }
    return (hash_value);
}

/**
 * Create a new hash table.
 *
 * @param nbuckets -- number of buckets to create
 * @param hash_function -- pointer to the hashing function to be used
 *
 * @returns pointer to new hash table.
 */

icl_hash_t * icl_hash_create( int nbuckets, unsigned int (*hash_function)(void*), int (*hash_key_compare)(void*, void*))
{
  icl_hash_t *ht;
  int i;

  ht = (icl_hash_t*) my_malloc(sizeof(icl_hash_t));
  assert(ht!=NULL);
  if(!ht) return NULL;

  ht->nentries = 0;
  ht->buckets = (icl_entry_t**)my_malloc(nbuckets * sizeof(icl_entry_t*));
  assert(ht->buckets!=NULL);
  if(!ht->buckets) return NULL;

  ht->nbuckets = nbuckets;
  for(i=0;i<ht->nbuckets;i++)
      ht->buckets[i] = NULL;

  ht->hash_function = hash_pjw;
  ht->hash_key_compare = hash_key_compare ? hash_key_compare : string_compare;

  return ht;
}


/**
 * Search for an entry in a hash table.
 *
 * @param ht -- the hash table to be searched
 * @param key -- the key of the item to search for
 *
 * @returns pointer to the entry corresponding to the key.
 *   If the key was not found, returns NULL.
 */
icl_entry_t * icl_hash_find(icl_hash_t *ht, void* key)
{
    icl_entry_t* curr;
    unsigned int hash_val;

    if(!ht || !key) return NULL;

    hash_val = (* ht->hash_function)(key) % ht->nbuckets;
    for (curr=ht->buckets[hash_val]; curr != NULL; curr=curr->next){
        if ( ht->hash_key_compare(curr->key, key))
            return curr;
    }
    return NULL;
}

/**
 * Replace entry in hash table with the given entry.
 *
 * @param ht -- the hash table
 * @param key -- the key of the new item
 * @param data -- pointer to the new item's data
 * @param oldnode -- pointer to the old item (set upon return)
 *
 * @returns pointer to the new item.  Returns NULL on error.
 */

icl_entry_t * icl_hash_update_insert(icl_hash_t *ht, void* key, void *data, void **oldnode)
{
    icl_entry_t *curr, *prev;
    unsigned int hash_val;
    icl_entry_t * out;
    if(!ht || !key) return NULL;
    hash_val = (* ht->hash_function)(key) % ht->nbuckets;
    printf("hash_val %d\n", hash_val);
    /* Scan bucket[hash_val] for key */
    for (prev=NULL,curr=ht->buckets[hash_val]; curr != NULL; prev=curr, curr=curr->next){
        /* If key found, remove node from list and setup oldnode for the return */
        if ( ht->hash_key_compare(curr->key, key)) {
            if(oldnode != NULL)
                *oldnode = curr;
            ht->nentries--;
            if (prev == NULL)
                ht->buckets[hash_val] = curr->next;
            else
                prev->next = curr->next;
        }
    }
    /* Since key was either not found, or found-and-removed, create and prepend new node */
    out = (icl_entry_t*)my_malloc(sizeof(icl_entry_t));

    out->key = key;
    out->data = data;
    out->next = ht->buckets[hash_val]; /* add at start */

    ht->buckets[hash_val] = out;
    ht->nentries++;

    return out;
}

/**
 * Free one hash table entry located by key (key and data are freed using functions).
 *
 * @param ht -- the hash table to be freed
 * @param key -- the key of the new item
 * @param free_key -- pointer to function that frees the key
 * @param free_data -- pointer to function that frees the data
 *
 * @returns 0 on success, -1 on failure.
 */
int icl_hash_delete(icl_hash_t *ht, void* key, void (*free_key)(void*), void (*free_data)(void*))
{
    icl_entry_t *curr, *prev;
    unsigned int hash_val;

    if(!ht || !key) return -1;
    hash_val = (* ht->hash_function)(key) % ht->nbuckets;

    prev = NULL;
    for (curr=ht->buckets[hash_val]; curr != NULL; )  {
        if ( ht->hash_key_compare(curr->key, key)) {
            if (prev == NULL) {
                ht->buckets[hash_val] = curr->next;
            } else {
                prev->next = curr->next;
            }
            if (*free_key && curr->key) (*free_key)(curr->key);
            if (*free_data && curr->data) (*free_data)(curr->data);
            ht->nentries--;
            my_free(curr);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    return -1;
}

/**
 * Free hash table structures (key and data are freed using functions).
 *
 * @param ht -- the hash table to be freed
 * @param free_key -- pointer to function that frees the key
 * @param free_data -- pointer to function that frees the data
 *
 * @returns 0 on success, -1 on failure.
 */
int icl_hash_destroy(icl_hash_t *ht, void (*free_key)(void*), void (*free_data)(void*))
{
    icl_entry_t *bucket, *curr, *next;
    int i;

    if(!ht) return -1;

    for (i=0; i<ht->nbuckets; i++) {
        bucket = ht->buckets[i];
        for (curr=bucket; curr!=NULL; ) {
            next=curr->next;
            if (*free_key && curr->key) (*free_key)(curr->key);
            if (*free_data && curr->data) (*free_data)(curr->data);
            my_free(curr);
            curr=next;
        }
    }

    if(ht->buckets) my_free(ht->buckets);
    if(ht) my_free(ht);

    return 0;
}


/**
 * Dump the hash table's contents to the given file pointer.
 *
 * @param stream -- the file to which the hash table should be dumped
 * @param ht -- the hash table to be dumped
 *
 * @returns 0 on success, -1 on failure.
 */

int icl_hash_dump(FILE* stream, icl_hash_t* ht)
{
    icl_entry_t *bucket, *curr;
    int i;

    if(!ht) return -1;

    for(i=0; i<ht->nbuckets; i++) {
        bucket = ht->buckets[i];
        for(curr=bucket; curr!=NULL; ) {
            if(curr->key)
                fprintf(stream, "icl_hash_dump: %s: %p\n", (char *)curr->key, curr->data);
            curr=curr->next;
        }
    }
    return 0;
}

/**
 * @brief Applica una funzione alle entries della hash table
 *        fintantochè la funzione restituisce 0
 *
 * @param tab -- hash table
 * @param mux -- array delle mutex della ht
 * @param vfun -- funzione da applicare alle entries
 * @param argv -- argomenti aggiuntivi necessari alla vfun
 */
void icl_hash_apply_until(icl_hash_t * tab, pthread_mutex_t * mux, int vfun(icl_entry_t * corr, void ** arg), void ** argv)
{
    icl_entry_t * corr = NULL;
    int i;
    int found = 0;

    unsigned int curr_lock;
    for(i=0; i<tab->nbuckets; i++){

        curr_lock = hash_lock(i);
        if(curr_lock != hash_lock(i-1))
            PTHREAD_MUTEX_LOCK(&mux[curr_lock], "icl_hash_tolist mux");

        corr = tab->buckets[i];
        while(corr != NULL && !found){
            if(vfun(corr, argv) != 0)
                found = 1;
            else
                corr = corr->next;
        }

        if(curr_lock != hash_lock(i-1))
            PTHREAD_MUTEX_UNLOCK(&mux[curr_lock], "icl_hash_tolist mux");
        if(found == 1)
            break;
    }
}
