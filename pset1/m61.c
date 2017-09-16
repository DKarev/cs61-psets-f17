#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>


typedef struct
{
    void* pntr;
    int freed;
    const char* file;
    int line;
    size_t sz;

}Pointers;

int list_size=0;
Pointers list_pntrs[10000005];

size_t sz_add=32;
struct m61_statistics stats_global = {0,0,0,0,0,0,NULL,NULL};


//The list that we need for heavy hitter
typedef struct HeavyHitterList
{
    const char* file;
    int line;
    long long f;          //the computed (estimated) frequency of the item
    long long bytes;      //the computed (estimated) number of bytes allocated
    long long delta_f;      //the maximum possible error in f (  f_real - f   <=   delta_f )
    long long delta_bytes;  //the maximum possible error in bytes ( bytes_real - bytes <= delta_bytes )

    struct HeavyHitterList *next; //pointer to the next element
    struct HeavyHitterList *prev; //pointer to the previous element

}HeavyHitterList;

HeavyHitterList *head_f=NULL, *tail_f=NULL;
HeavyHitterList *head_bytes=NULL, *tail_bytes=NULL;

//Number of items processed and the current bucket
long long N_f=0, N_bytes=0, bucket_f=1, bucket_bytes=1;
const double threshold=0.10;
const double error=0.001; // possible error in answer
const long long cycle = (int) (1.0/error); //capacity of each bucket


//We are using Lossy Counting Alorithm to find high frequency pointers:
void m61_hhreport_frequencies(const char* file, int line)
{
    int in_the_list=0,deleted=0;
    N_f++;

    //check if the pointer is in the list
    for( HeavyHitterList *pntr=head_f; pntr != NULL ; pntr=pntr->next )
    {

        if(pntr->file == file && pntr->line == line)
        {
            pntr->f++;
            in_the_list=1;
            break;
        }
    }

    //if it is not in the list, add it
    if(in_the_list==0)
    {
        HeavyHitterList *pntr = base_malloc(sizeof(HeavyHitterList));

        pntr->f = 1; 
        pntr->file = file;
        pntr->line = line;
        pntr->delta_f = bucket_f;
        pntr->next = NULL;


        if(head_f == NULL)
        {
            pntr->prev=NULL;
            head_f=pntr;
            tail_f=pntr;
        }

        else
        {
            tail_f->next=pntr;
            pntr->prev=tail_f;
        
            tail_f=pntr;
        }
    }


    //check if we are still in the same bucket
    if(N_f%cycle==0)
    {
        
        //if not, delete all infrequent elements
        for( HeavyHitterList *pntr=head_f; pntr != NULL ; pntr=pntr->next)
        {
            //checks to avoid undefined behavior
            if(deleted==1)pntr=pntr->prev;
            deleted=0;

            if(pntr->f + pntr->delta_f  <= bucket_f)
            {
                if(pntr->prev!=NULL)pntr->prev->next = pntr->next;
                else head_f=pntr->next;
                if(pntr->next!=NULL)pntr->next->prev = pntr->prev;
                else tail_f=pntr->prev;

                HeavyHitterList *help_pntr = pntr;
                pntr=pntr->prev;
                if(pntr==NULL){pntr=help_pntr->next;deleted=1;}

                base_free(help_pntr);
                
            }
        }
        bucket_f++;
    }

}

//Same approach here except that we are using weighted frequencies so we can fill many buckets with only one pointer
void m61_hhreport_bytes(size_t sz, const char* file, int line)
{
    int in_the_list=0,deleted=0;
    N_bytes+=sz;

    for( HeavyHitterList *pntr=head_bytes; pntr != NULL ; pntr=pntr->next )
    {

        if(pntr->file == file && pntr->line == line)
        {
            pntr->bytes+=sz;
            in_the_list=1;
            break;
        }
    }

    if(in_the_list==0)
    {
        HeavyHitterList *pntr = base_malloc(sizeof(HeavyHitterList));

        pntr->bytes = sz;
        pntr->file = file;
        pntr->line = line;
        pntr->delta_bytes = bucket_bytes;
        pntr->next = NULL;

        if(head_bytes == NULL)
        {
            pntr->prev = NULL;
            head_bytes =pntr;
            tail_bytes = pntr;
        }

        else
        {
            tail_bytes->next = pntr;
            pntr->prev = tail_bytes;
        
            tail_bytes = pntr;
        }
    }



    //if we have filled one or more buckets
    if(N_bytes/cycle>=bucket_bytes)
    {
       
       for( HeavyHitterList *pntr=head_bytes; pntr != NULL ; pntr=pntr->next)
        {
            if(deleted==1)pntr=pntr->prev;
            deleted=0;

            if(pntr->bytes + pntr->delta_bytes  <= bucket_bytes)
            {
                if( pntr->prev != NULL ) pntr->prev->next = pntr->next;
                else head_bytes=pntr->next;
                if( pntr->next != NULL ) pntr->next->prev = pntr->prev;
                else tail_bytes=pntr->prev;


                HeavyHitterList *help_pntr = pntr;

                pntr=pntr->prev;
                if(pntr==NULL){pntr=help_pntr->next;deleted=1;}
                
                base_free(help_pntr);
    
            }
        }	
        bucket_bytes=N_bytes/cycle + 1;
    }

}

/// m61_malloc(sz, file, line)
///    Return a pointer to `sz` bytes of newly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then m61_malloc may
///    either return NULL or a unique, newly-allocated pointer value.
///    The allocation request was at location `file`:`line`.

void* m61_malloc(size_t sz, const char* file, int line)
{
    (void) file, (void) line;   


    //Making sure that sz+sz_add does not overflow.
    if(sz+sz_add<sz){stats_global.fail_size+=sz; stats_global.nfail++; return NULL;}


    //Allocating enough memory for metadata (sz_add) and boundary write detection. Also, making sure it is divisible by 16.
    size_t sz_total = sz + sz_add + ( 16 - (sz + sz_add + 16) % 16 );
    if( (sz + sz_add + 16) % 16  >  8 ) sz_total += 16;


    void* result = base_malloc( sz_total) ;
    if(result==NULL) {stats_global.fail_size+=sz; stats_global.nfail++; return NULL;}

    

    


    //Initializing heap_min and heap_max
    if(stats_global.ntotal==0)
    {
        stats_global.heap_min=result;
        stats_global.heap_max=result+sz+sz_add;
    }

    if( stats_global.heap_max < (char*)result + sz + sz_add ) { stats_global.heap_max = (char*)result + sz + sz_add; }
    if( stats_global.heap_min > (char*)result ) { stats_global.heap_min = (char*)result; } 

    stats_global.ntotal++;
    stats_global.nactive++;
    stats_global.total_size += sz;
    stats_global.active_size += sz;

    m61_hhreport_frequencies(file, line);
    m61_hhreport_bytes(sz, file, line);

    //listing all of the active memory
    if(list_size<500)
    {
        list_pntrs [ list_size ].pntr  =  result + sz_add;
        list_pntrs [ list_size ].freed  =  0;
        list_pntrs [ list_size ].file = file;
        list_pntrs [ list_size ].line = line;
        list_pntrs [ list_size ].sz = sz;
        list_size++;
    }



    //Metadating the size
    long long* ptr1 = result;
    (*ptr1)=(long long)sz;


    //Metadating the address (test 35)
    ptr1 = result + 8;
    (*ptr1) = (long long)(result + sz_add);



    //Putting random garbage after the last memory block so we can detect if it changes
    unsigned char* help_ptr = result + sz_add + sz;
    (*help_ptr) = 42;

    //Putting random garbage before the first memory block so we can detect double frees and invalid frees
    help_ptr = result + sz_add - 1;
    (*help_ptr) = 42;

    //Metadating for the index in the list of active memory 
    unsigned int* help_ptr1 = result + sz_add - 8;
    (*help_ptr1) = list_size-1;


    return result + sz_add;
}


/// m61_bug_detector(ptr, file, line, type)
///    Checks for most known bugs. If type=0, it is called from free(),
///    therefore, if ptr is found, mark it as freed. If type=1, don't mark it as freed.

void m61_bug_detector(void *ptr, const char* file, int line, int type)
{

    //Checking if the pointer is within the boundaries set by heap_min/heap_max
    if( (char*)ptr < stats_global.heap_min || (char*)ptr > stats_global.heap_max )
    {
        printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not in heap\n", file, line, ptr );
        abort(); return;
    }


    //checking if that is the expected address and whether this pointer has been allocated
    unsigned char* beforefirst_val = ptr - 1;
    long long* expected_adress = ptr - sz_add + 8;

    
    if(*beforefirst_val != 42 || (*expected_adress) != (long long)ptr)
    {



        //the pointer has not been allocated; checking whether it is inside another block of memory 
        int inside=0, insideind;

        for(int i=0;i<list_size;i++)
        {
            if( list_pntrs[i].pntr < ptr && ptr < (list_pntrs[i].pntr + list_pntrs[i].sz ) ) { inside=1; insideind=i; }
        }


        if(inside==1)
        {

            printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n",file, line, ptr );
            printf("  %s:%d: %p is %zu bytes inside a %zu byte region allocated here\n", file, list_pntrs [ insideind ].line, ptr, ptr - list_pntrs[ insideind ].pntr, list_pntrs[ insideind ].sz );
            abort();
            return;
        }

        printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n",file, line, ptr ); abort(); return;
    }


    
    //Checking for double frees 
    if(type==0 && list_size<500)
    {
        //printf("Nice\n" );
        unsigned int* listind_val = ptr - 8;
        if(list_pntrs[*listind_val].freed==1){printf("MEMORY BUG: %s:%d: invalid free of pointer %p\n",file, line, ptr ); abort(); return;}
        list_pntrs[*listind_val].freed=1;
    }



    //Checking for our-of-boundary writing
    unsigned long* mem_sz = ptr - sz_add; 
    unsigned char* afterlast_val = ptr + *mem_sz;
    
    if( *afterlast_val != 42 ) {printf("MEMORY BUG: %s:%d: detected wild write during free of pointer %p\n",file, line, ptr ); abort(); return;}
   

}



/// m61_free(ptr, file, line)
///    Free the memory space pointed to by `ptr`, which must have been
///    returned by a previous call to m61_malloc and friends. If
///    `ptr == NULL`, does nothing. The free was called at location
///    `file`:`line`.

void m61_free(void *ptr, const char *file, int line)
{
    (void) file, (void) line;

    if(ptr==NULL)return;


    m61_bug_detector(ptr, file, line, 0);


    //Managing statistics
    long long* ptr1=ptr-sz_add;
    stats_global.nactive--;
    stats_global.active_size-= (*ptr1);

    base_free(ptr1);
}


/// m61_realloc(ptr, sz, file, line)
///    Reallocate the dynamic memory pointed to by `ptr` to hold at least
///    `sz` bytes, returning a pointer to the new block. If `ptr` is NULL,
///    behaves like `m61_malloc(sz, file, line)`. If `sz` is 0, behaves
///    like `m61_free(ptr, file, line)`. The allocation request was at
///    location `file`:`line`.

void* m61_realloc(void* ptr, size_t sz, const char* file, int line)
{
    void* new_ptr = NULL;

    size_t* used_size;

    if(ptr!=NULL)m61_bug_detector(ptr, file, line, 1);


    if (sz)
    {
        new_ptr = m61_malloc(sz, file, line);
    }

    if (ptr && new_ptr)
    {

        used_size = ptr - sz_add;
        
        //If the size used by ptr is more than the newly requested one, don't copy everything.
        if(sz < (*used_size)) memcpy(new_ptr, ptr, sz);
        else memcpy(new_ptr, ptr, (*used_size));

    }
    
    m61_free(ptr, file, line);
    
    return new_ptr;
}


/// m61_calloc(nmemb, sz, file, line)
///    Return a pointer to newly-allocated dynamic memory big enough to
///    hold an array of `nmemb` elements of `sz` bytes each. The memory
///    is initialized to zero. If `sz == 0`, then m61_malloc may
///    either return NULL or a unique, newly-allocated pointer value.
///    The allocation request was at location `file`:`line`.

void* m61_calloc(size_t nmemb, size_t sz, const char* file, int line)
{
    void* ptr;

    //Making sure that nmemb*sz is not overflowing 
    if((nmemb*sz)/sz == nmemb)ptr = m61_malloc(nmemb * sz, file, line);    
    else {stats_global.fail_size+=nmemb;stats_global.nfail++;return NULL;}

    //Initializing to zero
    if (ptr)memset(ptr, 0, nmemb * sz);

    return ptr;
}


/// m61_getstatistics(stats)
///    Store the current memory statistics in `*stats`.

void m61_getstatistics(struct m61_statistics* stats)
{
    
    stats->nactive = stats_global.nactive;
    stats->ntotal = stats_global.ntotal;
    stats->nfail = stats_global.nfail;
    stats->active_size = stats_global.active_size;
    stats->total_size = stats_global.total_size;
    stats->fail_size = stats_global.fail_size;
    stats->heap_min = stats_global.heap_min;
    stats->heap_max = stats_global.heap_max;
}


/// m61_printstatistics()
///    Print the current memory statistics.

void m61_printstatistics(void)
{
    struct m61_statistics stats;
    m61_getstatistics(&stats);

    printf("malloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("malloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}


/// m61_printleakreport()
///    Print a report of all currently-active allocated blocks of dynamic
///    memory.

void m61_printleakreport(void)
{
    for(int i=0;i<list_size;i++)
    {
        //printf("%d\n", list_size );
        if(list_pntrs[i].freed==0)
        {
            printf("LEAK CHECK: %s:%d: allocated object %p with size %zu\n",list_pntrs[i].file, list_pntrs[i].line, list_pntrs[i].pntr, list_pntrs[i].sz);
        }
    }
}

void m61_printhhreport(void)
{
    HeavyHitterList *MaxPntr;
    long long Max=0;

    //Selection sort to print the results for the heaviest pointers.
    while(1)
    {

        if(head_bytes==NULL)break;

        Max=0;
        for( HeavyHitterList *pntr=head_bytes; pntr != NULL ; pntr=pntr->next )
        {
            if(pntr->bytes>Max){Max=pntr->bytes;MaxPntr=pntr;}    
        }

        if(Max >=  (threshold-error)*N_bytes)
        {
                printf("High weight pointer: %s:%d: %2.0f%% of total memory allocated\n", MaxPntr->file, MaxPntr->line, MaxPntr->bytes*100.0/(double)N_bytes);
                MaxPntr->bytes=0;
        }
        else break;
    }



    //Selection sort to print the results for the most frequent pointers
    while(1)
    {

        if(head_f==NULL)break;

        Max=0;
        for( HeavyHitterList *pntr=head_f; pntr != NULL ; pntr=pntr->next )
        {
            if(pntr->f>Max){Max=pntr->f;MaxPntr=pntr;}    
        }

        if(Max >=  (threshold-error)*N_f)
        {
                printf("High frequency pointer: %s:%d: %2.0f%% of total allocation calls\n", MaxPntr->file, MaxPntr->line, MaxPntr->f*100.0/(double)N_f);
                MaxPntr->f=0;
        }
        else break;
    }
}

