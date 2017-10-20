#include "io61.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <sys/mman.h>


// io61.c
//    YOUR CODE HERE!


// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

const size_t CACHESIZE=(1<<12);

struct io61_file {
    int fd;
    char* cache;
    unsigned long long first, last;
    size_t cache_size;
    off_t pntr;
    off_t size;
    int flag;
    int was_seek;

};


size_t min(size_t a, size_t b)
{
    if(a<b)return a;
    return b;
}


// io61_fdopen(fd, mode)
//    Return a new io61_file for file descriptor `fd`. `mode` is
//    either O_RDONLY for a read-only file or O_WRONLY for a
//    write-only file. You need not support read/write files.


char *file_data;
io61_file* io61_fdopen(int fd, int mode) {
    assert(fd >= 0);
    io61_file* f = (io61_file*) malloc(sizeof(io61_file));
    
    f->fd = fd;
    f->pntr=0;
    f->cache = (char*) malloc(CACHESIZE*sizeof(char));
    f->cache_size=0;
    f->first=-1;
    f->last=-1;
    f->size = io61_filesize(f);
    f->was_seek=0;


    file_data = mmap(NULL, f->size, PROT_WRITE, MAP_SHARED, f->fd, 0);


    (void) mode;
    return f;
}


// io61_close(f)
//    Close the io61_file `f` and release all its resources.

int io61_close(io61_file* f) {
    io61_flush(f);    
    free(f->cache);

    int r = close(f->fd);
    free(f);
    return r;
}



// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file* f) {

    
    unsigned char buf[1];

    f->flag=0;
    size_t read_size = 1;
    off_t safe;


    

    //char* file_data = mmap(NULL, 1, PROT_WRITE, MAP_SHARED, f->fd, 0);
    if (file_data == MAP_FAILED)
    {
        //If the whole block is in the cache
        if(f->first <= (unsigned long long)f->pntr  &&  (unsigned long long)f->pntr <= f->last)
        {
            memcpy(buf,  f->cache + (f->pntr - f->first),  1);
            f->pntr ++;

            f->was_seek=0;
            //printf("%zu\n", br);
            //if(br==1)printf("YES\n");
            return buf[0];
        }

        if(f->was_seek)
        {
            safe = f->pntr;
            if( (size_t)f->pntr >= CACHESIZE ) io61_seek(f, f->pntr - CACHESIZE+1);
            else io61_seek(f,0);

        }

        read_size = read(f->fd, f->cache , CACHESIZE);

        f->first = f->pntr;
        f->last = f->pntr + read_size - 1;

        if(read_size==0)return EOF;
            
        if(f->was_seek==0)
        {
            memcpy(buf, f->cache, 1); 
            f->pntr ++;
            
        }   
        else
        {
            memcpy(buf, f->cache + CACHESIZE-1, 1);
            io61_seek(f,safe);
            f->was_seek=0;
        }

       return buf[0];


    }

    memcpy(buf, file_data, 1);
    f->pntr+=read_size;
    return buf[0];


}


// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count, which might be zero, if the file ended before `sz` characters
//    could be read. Returns -1 if an error occurred before any characters
//    were read.

ssize_t io61_read(io61_file* f, char* buf, size_t sz)
{
    f->flag=0;
    size_t read_size = min(sz,f->size), nbytes;
    off_t safe;


    //return read(f->fd, buf, sz);

    //file_data = mmap(NULL, sz, PROT_WRITE, MAP_SHARED, f->fd, 0);
    if (file_data == MAP_FAILED)
    {
        f->was_seek=0;
        //if(sz)printf("%zu\n", sz );
        //If the whole block is in the cache
        
        if(f->first <= (size_t)f->pntr  &&  (size_t)f->pntr + sz - 1  <= f->last)
        {
            memcpy(buf,  f->cache + (f->pntr - f->first),  sz);
            f->pntr += sz;
            return sz;
        }

        if(f->first > (size_t)f->pntr || f->last < (size_t)f->pntr)
        {
            if(f->was_seek)
            {
                safe = f->pntr;
                if( (size_t)f->pntr >= CACHESIZE) io61_seek(f, f->pntr - CACHESIZE + sz);
                else io61_seek(f,0);

            }

            read_size = read(f->fd, f->cache , CACHESIZE);

            f->first = f->pntr;
            f->last = f->pntr + read_size - 1;
            
            if(f->was_seek==0)
            {
                memcpy(buf, f->cache, min(sz,read_size)); 
                f->pntr += min(sz,read_size);
            }
            
            else
            {
                memcpy(buf, f->cache + (safe - f->first), min(sz,read_size));
                io61_seek(f,safe);
                f->was_seek=0;
            }

           

            
            return min(sz,read_size);
        }


        nbytes = f->last - f->pntr + 1;
        memcpy(buf, f->cache + (f->pntr - f->first), nbytes);

        read_size = read(f->fd, f->cache , CACHESIZE);

        f->first = f->last+1;
        f->last = f->last + read_size;
        
        //printf("Hello\n");
        //printf("%zu %zu\n", sz, read_size );
        memcpy(buf+nbytes, f->cache, min(sz-nbytes,read_size));    
        

        f->was_seek=0;
        f->pntr += min(sz-nbytes,read_size)+nbytes;
        return min(sz-nbytes,read_size)+nbytes;

    }



    memcpy(buf, file_data, read_size);
    f->pntr+=read_size;
    return read_size;

}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file* f, int ch)
{

    io61_write(f, (const char*)(&ch), 1);
    return 0;
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file* f, const char* buf, size_t sz)
{
   
    f->flag=1;
    
    if(f->cache_size+sz>CACHESIZE)
    {
        memcpy(f->cache+f->cache_size, buf, CACHESIZE - f->cache_size );
        write(f->fd, f->cache, CACHESIZE);


        memcpy(f->cache, buf + CACHESIZE - f->cache_size, sz - CACHESIZE + f->cache_size );

        f->cache_size = sz - CACHESIZE + f->cache_size;

    }
    else
    {
        memcpy(f->cache+f->cache_size, buf, sz);
        f->cache_size+=sz;
    }


    return sz;
}


// io61_flush(f)
//    Forces a write of all buffered data written to `f`.
//    If `f` was opened read-only, io61_flush(f) may either drop all
//    data buffered for reading, or do nothing.

int io61_flush(io61_file* f)
{
    if(f->flag==1)
    {
        write(f->fd, f->cache, f->cache_size);
        f->cache_size=0;
    }
    (void) f;
    return 0;
}


// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file* f, off_t pos) {

    if(f->flag==1){io61_flush(f);}
    f->was_seek=1;

    f->pntr=pos;
    off_t r = lseek(f->fd, (off_t) pos, SEEK_SET);
    if (r == (off_t) pos) {
        return 0;
    } else {
        return -1;
    }
}


// You shouldn't need to change these functions.

// io61_open_check(filename, mode)
//    Open the file corresponding to `filename` and return its io61_file.
//    If `filename == NULL`, returns either the standard input or the
//    standard output, depending on `mode`. Exits with an error message if
//    `filename != NULL` and the named file cannot be opened.

io61_file* io61_open_check(const char* filename, int mode) {
    int fd;
    if (filename) {
        fd = open(filename, mode, 0666);
    } else if ((mode & O_ACCMODE) == O_RDONLY) {
        fd = STDIN_FILENO;
    } else {
        fd = STDOUT_FILENO;
    }
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(1);
    }
    return io61_fdopen(fd, mode & O_ACCMODE);
}


// io61_filesize(f)
//    Return the size of `f` in bytes. Returns -1 if `f` does not have a
//    well-defined size (for instance, if it is a pipe).

off_t io61_filesize(io61_file* f) {
    struct stat s;
    int r = fstat(f->fd, &s);
    if (r >= 0 && S_ISREG(s.st_mode)) {
        return s.st_size;
    } else {
        return -1;
    }
}


// io61_eof(f)
//    Test if readable file `f` is at end-of-file. Should only be called
//    immediately after a `read` call that returned 0 or -1.

int io61_eof(io61_file* f) {
    char x;
    ssize_t nread = read(f->fd, &x, 1);
    if (nread == 1) {
        fprintf(stderr, "Error: io61_eof called improperly\n\
  (Only call immediately after a read() that returned 0 or -1.)\n");
        abort();
    }
    return nread == 0;
}
