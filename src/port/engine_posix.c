/*
    Copyright (C) 2015-2023, Navaro, All Rights Reserved
    SPDX-License-Identifier: MIT

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
 */


/**
 * @file    strsub
 * @note    EXAMPLE! EXAMPLE! EXAMPLE!
 * @brief   Posix port for Engine
 *
 *
 *
 * @addtogroup
 * @{
 */

#include "engine_config.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#include "../engine.h"
#include "../parts/parts.h"
#if CFG_USE_STRSUB
#include "../common/strsub.h"
#include "../tool/parse.h"
#endif


#define ENGINE_MAX_VARIABLES            100

/*===========================================================================*/
/* Data structures and types.                                                */
/*===========================================================================*/

/*  A structure for a for an event queued by a part. */
typedef struct ENGINE_EVENT_S {
    struct ENGINE_EVENT_S * next ;
    time_t                  expire ;
    uint32_t                event ;
    intptr_t                parm ;
    int32_t                 event_register ;
    EVENT_TASK_CB           complete ;

} ENGINE_EVENT_T;

typedef struct ENGINE_EVENT_LIST_S {
    ENGINE_EVENT_T * head ;
} ENGINE_EVENT_LIST_T ;

/*===========================================================================*/
/* Static declarations.                                                */
/*===========================================================================*/

ENGINE_EVENT_LIST_T         _engine_event_list = {0} ;
static sem_t                _engine_event ;
static pthread_mutex_t      _engine_mutex ;
static pthread_t            _engine_thread ;
static bool                 _engine_quit = false ;
static const char *         _engine_config_file = 0 ;
static time_t               _engine_start_time = 0 ;
static int32_t              _engine_variables[ENGINE_MAX_VARIABLES] = {0} ;

#if CFG_USE_STRSUB
static int32_t              engine_strsub_cb (STRSUB_REPLACE_CB cb, const char * str, size_t len, uint32_t offset, uintptr_t arg) ;
static STRSUB_HANDLER_T     _engine_strsub ;
#if CFG_USE_REGISTRY
static int32_t              registry_strsub_cb (STRSUB_REPLACE_CB cb, const char * str, size_t len, uint32_t offset, uintptr_t arg) ;
static STRSUB_HANDLER_T     _registry_strsub ;
#endif
#endif

static uint32_t             _engine_alloc[2] = {0} ;
static uint32_t             _engine_alloc_max[2] = {0} ;


static time_t
engine_get_timestamp (void)
{
    uint64_t       ms; // Milliseconds
    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    ms = spec.tv_nsec / 1.0e6 ; // Convert nanoseconds to milliseconds
    s +=  ms / 1000 ;
    ms %= 1000 ;

    return ms + s * 1000 ;

}

static void
remove_event (ENGINE_EVENT_T * task)
{
    if (!_engine_event_list.head) {
        return ;

    }

    engine_port_lock () ;

    ENGINE_EVENT_T **p = &_engine_event_list.head;
    bool signal = _engine_event_list.head == task ;

    while (*p != task)
            p = &(*p)->next;
    *p = task->next;


    engine_port_unlock () ;

    if (signal) sem_post (&_engine_event) ;

}

static void
insert_event (ENGINE_EVENT_T * task)
{
    ENGINE_EVENT_T  * start = _engine_event_list.head ;
    ENGINE_EVENT_T  * previous = 0 ;
    bool signal = false ;

    engine_port_lock () ;

    for (  ;
            (start!=0) &&
            ((int32_t)(task->expire - start->expire) >= 0);
        ) {

        previous = start ;
        start = start->next ;
        (void) start->next ;

    }

    if (previous == 0) {
        task->next = _engine_event_list.head ;
        _engine_event_list.head = task ;
        signal = true ;


    } else {
        previous->next = task ;
        task->next = start ;

    }

    engine_port_unlock () ;

    if (signal) sem_post (&_engine_event) ;

}

static void *
engine_thread (void *ptr)
{
    time_t next  ;
    int err ;
    struct timespec t ;



    while( !_engine_quit )
    {
        engine_port_lock () ;

        if (_engine_event_list.head) {
            next = _engine_event_list.head->expire - engine_get_timestamp() ;
            while (_engine_event_list.head &&
                    (next <= 0)
                ) {

                ENGINE_EVENT_T * task = _engine_event_list.head ;

                DBG_ENGINE_LOG (ENGINE_LOG_TYPE_PORT,
                        "[prt] event '%s' (%d)",
                        parts_get_event_name ((uint16_t)task->event), next);

                task->complete (task, task->event, task->event_register, task->parm) ;


                _engine_event_list.head = task->next ;
                free (task) ;

                if (_engine_event_list.head) {
                    next = _engine_event_list.head->expire - engine_get_timestamp() ;

                }

            }

        } else {
            next = 0 ;

        }

        if (_engine_event_list.head) {
            next = _engine_event_list.head->expire  ;

        } else {
            next = 0 ;

        }

        engine_port_unlock () ;


        if (next) {
            int val ;
            sem_getvalue(&_engine_event, &val) ;
            DBG_ENGINE_LOG (ENGINE_LOG_TYPE_PORT,
                    "[prt] wait for %dms (sem %d)",
                    (int)(next - engine_get_timestamp()), val) ;

            t.tv_sec = next / 1000 ;
            t.tv_nsec = ((long long)next * 1000000) % 1000000000 ;
            err = sem_timedwait(&_engine_event, &t);

        } else {
            int val ;
            sem_getvalue(&_engine_event, &val) ;
            DBG_ENGINE_LOG (ENGINE_LOG_TYPE_PORT,
                    "[prt] wait for event (sem %d)",
                    val) ;

            err = sem_wait(&_engine_event) ;

        }

        if (err < 0) {
            err = errno ;

            switch (err) {
                // The thread got ownership of the mutex
                case ETIMEDOUT:
                    DBG_ENGINE_LOG (ENGINE_LOG_TYPE_PORT,
                            "[prt] timeout!") ;
                    break;

                default:
                    DBG_ENGINE_LOG (ENGINE_LOG_TYPE_PORT,
                            "[prt] sem_wait returned %d - quit!", err) ;
                    return 0 ;

            }
        } else {
            DBG_ENGINE_LOG (ENGINE_LOG_TYPE_PORT,
                    "[prt] signaled!") ;

        }

    }

    return 0;
}


int32_t
engine_port_init (void * arg)
{
    _engine_config_file = (const char*) arg ;

#if CFG_USE_STRSUB
    /* This will replace variables and registers such as [a] with their actual
       value. */
    strsub_install_handler(0, StrsubToken1, &_engine_strsub, engine_strsub_cb) ;
#if CFG_USE_REGISTRY
    /*  This will replace registry entries written as [<name>] with their
       string or integer value. */
    strsub_install_handler(0, StrsubToken1, &_registry_strsub, registry_strsub_cb) ;
#endif
#endif

    return ENGINE_OK ;
}

int32_t
engine_port_start (void)
{
    pthread_mutexattr_t Attr;

    _engine_start_time = engine_get_timestamp () ;
    _engine_quit = false ;

    pthread_mutexattr_init (&Attr) ;
    pthread_mutexattr_settype (&Attr, PTHREAD_MUTEX_RECURSIVE) ;
    if (pthread_mutex_init (&_engine_mutex, &Attr) != 0) {
        DBG_ENGINE_LOG (ENGINE_LOG_TYPE_ERROR, "port: create mutex failed!") ;
        return ENGINE_FAIL;

    }
    pthread_mutexattr_destroy (&Attr);

    if (sem_init(&_engine_event, 0, 0) != 0) {
        DBG_ENGINE_LOG (ENGINE_LOG_TYPE_ERROR, "port: create sem failed!") ;
        return ENGINE_FAIL ;

    }

    if (pthread_create( &_engine_thread, NULL, engine_thread, (void*) 0) != 0) {
        DBG_ENGINE_LOG (ENGINE_LOG_TYPE_ERROR, "port: create thread failed!") ;
        return ENGINE_FAIL ;

    }

    return ENGINE_OK ;
}


void
engine_port_stop (void)
{
    _engine_quit = 1 ;
    sem_post (&_engine_event) ;
    pthread_join(_engine_thread, 0);
    sem_destroy(&_engine_event);
    pthread_mutex_destroy(&_engine_mutex);
}

void
engine_port_lock (void)
{
    pthread_mutex_lock (&_engine_mutex) ;
}

void
engine_port_unlock (void)
{
    pthread_mutex_unlock (&_engine_mutex) ;
}

int32_t
engine_port_variable_write (uint32_t idx, int32_t val)
{
    if (idx >= ENGINE_MAX_VARIABLES) {
        return ENGINE_PARM ;

    }

    _engine_variables[idx] = val ;
    return ENGINE_OK ;
}

int32_t
engine_port_variable_read (uint32_t idx, int32_t * val)
{
    if (idx >= ENGINE_MAX_VARIABLES) {
        return ENGINE_PARM ;

    }

    *val = _engine_variables[idx] ;
    return ENGINE_OK ;
}

void*
engine_port_malloc (portheap heap, uint32_t size)
{
    uint32_t * mem = malloc(size + sizeof(uint32_t)) ;
    if (mem) {
        _engine_alloc[heap] += size ;
        if (_engine_alloc_max[heap] < _engine_alloc[heap]) {
            _engine_alloc_max[heap] = _engine_alloc[heap] ;
        }
        *mem = size ;
        return mem + 1 ;

    }

    return 0 ;
}

void
engine_port_free (portheap heap, void* mem)
{
    if (mem) {
        uint32_t * pmem = (uint32_t*)mem - 1 ;
        _engine_alloc[heap] -= *pmem ;
        free (pmem) ;
    }
}

void
engine_log_mem_usage (void)
{
    DBG_ENGINE_LOG (ENGINE_LOG_TYPE_ERROR,
            "port: alloc %u machine bytes (%u max)",
            _engine_alloc[heapMachine], _engine_alloc_max[heapMachine]) ;
    DBG_ENGINE_LOG (ENGINE_LOG_TYPE_ERROR,
            "port: alloc %u parser bytes (%u max)",
            _engine_alloc[heapParser], _engine_alloc_max[heapParser]) ;

}

PENGINE_EVENT_T
engine_port_event_create (EVENT_TASK_CB complete)
{
    ENGINE_EVENT_T * task = malloc(sizeof(ENGINE_EVENT_T)) ;
    memset (task, 0, sizeof(ENGINE_EVENT_T)) ;
    task->complete = complete ;
    return (PENGINE_EVENT_T)task ;
}

int32_t
engine_port_event_queue (PENGINE_EVENT_T task, uint16_t event,
        int32_t reg, uintptr_t parm, int32_t timeout)
{
    if (timeout < 0) {
        free (task) ;
        return ENGINE_FAIL ;

    }

    ENGINE_EVENT_T * t = (ENGINE_EVENT_T*)task ;
    t->event = event ;
    task->event_register = reg ;
    task->parm = parm ;
    task->expire = engine_get_timestamp() + timeout ;

    insert_event (task) ;

    return ENGINE_OK ;
}

int32_t
engine_port_event_cancel (PENGINE_EVENT_T event)
{
    uint64_t now = engine_get_timestamp() ;
    uint32_t remaining =  0 ;
    if (event->expire > now)  {
        remaining = (uint32_t) (event->expire - now) ;

    }

    remove_event (event) ;
    free (event) ;

    return remaining ;
}

void
engine_port_log (int inst, const char *format_str, va_list  args)
{
    uint32_t uptime = (uint32_t)(engine_get_timestamp () - _engine_start_time) ;

    printf ("%.5u.%03u: %2d ", uptime/1000, uptime%1000, inst) ;
    vprintf (format_str, args) ;
    size_t len = strlen(format_str) ;

    if (format_str[len-1] != '\n') printf ("\r\n") ;

}

void
engine_port_assert (const char *msg)
{
    DBG_ENGINE_LOG (ENGINE_LOG_TYPE_PORT, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!") ;
    DBG_ENGINE_LOG (ENGINE_LOG_TYPE_PORT, msg) ;
    DBG_ENGINE_LOG (ENGINE_LOG_TYPE_PORT, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n") ;

    assert (0) ;
}

int32_t
engine_port_shellcmd (const char* shellcmd)
{
    return ENGINE_NOT_IMPL ;
}



uint32_t
engine_timestamp (void)
{
    return (uint32_t)engine_get_timestamp () ;
}

#if CFG_USE_REGISTRY

static inline bool
_isspace(int c)
{
    return (c == '\r') || (c == '\n') || (c == '\t') || (c == ' ') ;
}

char *read_string(char const *desired_name, size_t len) {
    char name[128];
    char val[128];
    char * str = NULL ;
    FILE * fp;
    fp = fopen(_engine_config_file, "r");
    if (fp == NULL) {
        return NULL ;

    }

    while (fscanf(fp, "%127[^=]=%127[^\n]%*c", name, val) == 2) {
        char* pname = &name[0] ;
        while (_isspace((int)*pname)) pname++ ;
        if (0 == strncmp(pname, desired_name, len)) {
            char* pval = &val[0] ;
            while (_isspace((int)*pval)) pval++ ;
            str = strdup(pval);
            char * end = str + strlen(str) - 1;
            while((end >= str) && (_isspace((int)*end))) {
                *end = '\0' ;
                end-- ;
            }
            break ;

        }

    }
    fclose(fp);

    return str;
}

int32_t
registry_int32_get (const char*  id, int32_t* value)
{
    int32_t ret_val = ENGINE_NOTFOUND ;
    char *temp = read_string(id, strlen(id));
    if (temp) {
        *value = strtol(temp, 0, 10);
        ret_val = ENGINE_OK ;
        free(temp);

    }
    return ret_val ;
}

uint32_t
registry_string_get (const char*  id, char* value, unsigned int length )
{
    uint32_t len = 0 ;
    char *temp = read_string(id, strlen(id));
    if (temp) {
        len = strlen(temp) ;
        if (len>=length) {
            len = length - 1 ;

        }
        strncpy(value, temp, len) ;
        value[len-1] = '\0' ;
        free(temp);

    }

    return len ;
}


#if CFG_USE_STRSUB
int32_t
registry_strsub_cb(STRSUB_REPLACE_CB cb, const char * str, size_t len,
                    uint32_t offset, uintptr_t arg)
{
    int32_t res = -1 ;

    if (!isdigit((int)str[0])) {
        char *temp = read_string(str, len);
        if (temp) {
            int32_t dstlen = strlen(temp) ;
            res = cb (temp, dstlen, offset, arg) ;
            free(temp);

        }

    }

    return res ;
}
#endif
#endif

#if CFG_USE_STRSUB
int32_t
engine_strsub_cb (STRSUB_REPLACE_CB cb, const char * str, size_t len,
                uint32_t offset, uintptr_t arg)
{
    int32_t res = -1 ;
    uint32_t idx ;
    int32_t value = 0 ;
    char strvalue[16] ;

    /* if this is an digit inside square brackets ([x]) it is replaced with
       a register value */
    if (
            isdigit((int)str[0]) &&
            (sscanf(str, "%u", (unsigned int*)&idx) > 0)
        ) {
        res = engine_get_variable (0, idx, &value) ;
        if (res >= 0) {
            res = sprintf (strvalue, "%d", (int)value) ;
            if (res > 0) {
                res = cb (strvalue, res, offset, arg) ;

            }

        }

    }

    return res ;
}
#endif

#if CFG_USE_STRSUB
static int32_t
parse_strsub_cb(STRSUB_REPLACE_CB cb, const char * str, size_t len,
                uint32_t offset, uintptr_t arg)
{
#define SERVICES_STRSUB_BUFFER_LEN          12
    int32_t res = ENGINE_FAIL ;
    char buffer[SERVICES_STRSUB_BUFFER_LEN] ;

    uint32_t idx = 0 ;
    if (len) {
        if (isdigit((int)str[0])) {
            if (sscanf(str, "%u", (unsigned int*)&idx) <= 0) return res ;

        } else {
            if (!ParseGetIdentifierId(str, len, &idx)){
                return ENGINE_FAIL ;

            }

        }

    }

    res = snprintf (buffer, SERVICES_STRSUB_BUFFER_LEN, "[%u]",
            (unsigned int)idx) ;
    res = cb (buffer, res, offset, arg) ;

    return res ;
}
#endif

const char *
engine_port_sanitize_string (const char * string, uint32_t * plen)
{
#if CFG_USE_STRSUB

#define PORT_STRSUB_HANDLERS_TOKENS     {"[]", 0, 0}
#define PORT_STRSUB_ESCAPE_TOKEN        0

    #pragma GCC diagnostic ignored  "-Wmissing-braces"
    STRSUB_INSTANCE_T   strsub_instance = {PORT_STRSUB_ESCAPE_TOKEN, PORT_STRSUB_HANDLERS_TOKENS, {0}} ;
    STRSUB_HANDLER_T    strsub ;
    strsub_install_handler(&strsub_instance, StrsubToken1, &strsub, parse_strsub_cb) ;
    uint32_t dstlen = strsub_parse_get_dst_length (&strsub_instance, string, *plen) ;
    char * newname = engine_port_malloc(heapParser, dstlen) ;
    if (newname) {
        *plen = strsub_parse_string_to (&strsub_instance, string, *plen, newname, dstlen) ;
    }
    return newname ;
#else
    return string ;
#endif

}

void
engine_port_release_string (const char * string)
{
#if CFG_USE_STRSUB
    engine_port_free (heapParser, (void*)string) ;
#endif
}



