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


#include "engine_config.h"
#if CFG_PORT_CORAL
#include <stdio.h>
#include <string.h>
#include <coral-platform/os.h>
#include <coral-platform/common/lists.h>
#include <coral-services/services.h>
#include "../engine.h"
#include "../machine/parse.h"

#define SERVICE_ENGINE_TASK_QUEUE           SERVICE_PRIO_QUEUE2
#define ENGINE_TASK_HEAP_STORE          (1<<0)
#define ENGINE_TASK_STORE_CNT               20

/**
 * A structure for a task with a pointer to a state machine.
 */
typedef struct ENGINE_EVENT_S {
    SVC_TASKS_T             task ;
    uint32_t                event ;
    int32_t                 event_register ;
    EVENT_TASK_CB           complete ;
} ENGINE_EVENT_T;

static LISTS_STACK_DECL(    _engine_task_store) ;
static int32_t              _engine_task_store_cnt = -1 ;
static int32_t              _engine_task_store_alloc = 0 ;
static OS_MUTEX_DECL(       _engine_task_mutex);
static ENGINE_EVENT_T       _engine_task_store_heap[ENGINE_TASK_STORE_CNT] ;

#if CFG_UTILS_STRSUB
static int32_t              engine_strsub_cb (STRSUB_REPLACE_CB cb, const char * str, size_t len, uint32_t offset, uint32_t arg) ;
static STRSUB_HANDLER_T     _engine_strsub ;
#endif

static OS_MUTEX_DECL(       _engine_mutex);



static inline ENGINE_EVENT_T*
engine_task_alloc (void) {
#if !ENGINE_TASK_STORE_CNT
    return (ENGINE_EVENT_T*)heap_malloc (HEAP_SPACE, sizeof(ENGINE_EVENT_T)) ;
#else
    os_mutex_lock (&_engine_task_mutex) ;
    _engine_task_store_alloc++ ;
    //STATS_COUNTER_MAX(STATS_ENGINE_TASK_MAX,_engine_task_store_alloc) ;
    ENGINE_EVENT_T* task =0 ;
    task = stack_head(&_engine_task_store) ;
    if (task) {
        DBG_ENGINE_ASSERT (_engine_task_store_cnt,
                    "[err] ---> engine_task_alloc\r\n") ;
        _engine_task_store_cnt-- ;
        stack_remove_head(&_engine_task_store, OFFSETOF(SVC_TASKS_T, next)) ;
        svc_tasks_init_task (&task->task) ;
        svc_tasks_set_flags (&task->task, ENGINE_TASK_HEAP_STORE) ;
        os_mutex_unlock (&_engine_task_mutex) ;

    } else {
        os_mutex_unlock (&_engine_task_mutex) ;
        task = (ENGINE_EVENT_T*)heap_malloc (HEAP_SPACE, sizeof(ENGINE_EVENT_T)) ;
        svc_tasks_init_task (&task->task) ;

    }
    if (_engine_task_store_cnt <= 2) os_thread_sleep (8) ;
    return task ;
#endif
}

static inline void
engine_task_free(ENGINE_EVENT_T* task) {
#if !ENGINE_TASK_STORE_CNT
    heap_free(HEAP_SPACE_ENGINE, task) ;
#else
    os_mutex_lock (&_engine_task_mutex) ;
    _engine_task_store_alloc-- ;
    //if (_engine_task_store_cnt < ENGINE_TASK_STORE_CNT) {
    if (svc_tasks_get_flags(&task->task) & ENGINE_TASK_HEAP_STORE) {
        DBG_ENGINE_ASSERT (_engine_task_store_cnt<ENGINE_TASK_STORE_CNT,
                    "[err] ---> engine_task_free\r\n") ;
        _engine_task_store_cnt++ ;
        stack_add_head(&_engine_task_store, (plists_t)task, OFFSETOF(SVC_TASKS_T, next)) ;
        os_mutex_unlock (&_engine_task_mutex) ;
    } else {
        os_mutex_unlock (&_engine_task_mutex) ;
        heap_free(HEAP_SPACE, task) ;
    }
#endif
}

static inline void
engine_task_init(void) {
#if ENGINE_TASK_STORE_CNT
    if (_engine_task_store_cnt < 0) {
        for (_engine_task_store_cnt=0;
                _engine_task_store_cnt<ENGINE_TASK_STORE_CNT;
                _engine_task_store_cnt++) {
            ENGINE_EVENT_T* task = &_engine_task_store_heap[_engine_task_store_cnt] ;
            svc_tasks_init_task (&task->task) ;
            svc_tasks_set_flags(&task->task, 1) ;
            stack_add_head(&_engine_task_store, (plists_t)task, OFFSETOF(SVC_TASKS_T, next)) ;

        }

    }

#endif
}

static inline void
engine_task_wait(void) {
#if ENGINE_TASK_STORE_CNT
    int i ;
    for (i=0;
            (i<ENGINE_TASK_STORE_CNT) ;
            i++) {

        ENGINE_EVENT_T* task = &_engine_task_store_heap[i] ;
        svc_tasks_cancel_wait (&task->task, 500) ;
    }

    DBG_ENGINE_ASSERT (_engine_task_store_cnt==ENGINE_TASK_STORE_CNT,
                "[err] ---> engine_task_wait %d\r\n") ;

#endif
}

int32_t
engine_port_init (void)
{
    engine_task_init () ;
    os_mutex_init (&_engine_task_mutex) ;
    os_mutex_init (&_engine_mutex) ;

#if CFG_UTILS_STRSUB
    strsub_install_handler(0, StrsubToken1, &_engine_strsub, engine_strsub_cb) ;
#endif

    return ENGINE_OK ;
}

int32_t
engine_port_start (void)
{

    return ENGINE_OK ;
}

void
engine_port_stop (void)
{
    engine_task_wait () ;
}

void*
engine_port_malloc (portheap heap, uint32_t size)
{
    return heap_malloc (HEAP_SPACE, size) ;
}

void
engine_port_free (portheap heap, void* mem)
{
    heap_free (HEAP_SPACE, mem) ;
}

void
engine_port_lock (void)
{
    os_mutex_lock (&_engine_mutex) ;
}

void
engine_port_unlock (void)
{
    os_mutex_unlock (&_engine_mutex) ;
}

static void
port_event_queue_callback (SVC_TASKS_T * task, uintptr_t parm, uint32_t reason)
{
    ENGINE_EVENT_T*  engine = (ENGINE_EVENT_T*)task ;
    if (reason == SERVICE_CALLBACK_REASON_RUN) {
        engine->complete ((PENGINE_EVENT_T)task, engine->event, engine->event_register, parm) ;

    }

    svc_tasks_complete ((SVC_TASKS_T*)task) ;
    engine_task_free ((ENGINE_EVENT_T*)task) ;
}


PENGINE_EVENT_T
engine_port_event_create (EVENT_TASK_CB complete)
{
    ENGINE_EVENT_T * task = engine_task_alloc () ;

    if (!task) {
        svc_logger_log ("ENG   : : event allocation failed!!") ;
        return 0 ;

    }
    task->complete = complete ;


    return (PENGINE_EVENT_T)task ;
}

int32_t
engine_port_event_queue (PENGINE_EVENT_T event, uint16_t event_id,
        int32_t reg, uintptr_t parm, int32_t timeout)
{
    if (event) {
        ENGINE_EVENT_T * task = (ENGINE_EVENT_T*)event ;

        if (timeout>= 0) {

            task->event = event_id ;
            task->event_register = reg ;
            if  (svc_tasks_schedule (&task->task, port_event_queue_callback, parm,
                SERVICE_ENGINE_TASK_QUEUE, timeout ? SVC_TASK_MS2TICKS(timeout) : 0) == EOK) {
                return EOK ;

            }

        }

        engine_task_free (task) ;

     }

    return ENGINE_FAIL ;
}

int32_t
engine_port_event_cancel (PENGINE_EVENT_T event)
{
    int32_t expire = svc_task_expire (&event->task) ;
    if (svc_tasks_cancel (&event->task) != EOK) {
        expire = 0 ;
    }

    return expire ? SVC_TASK_TICKS2MS(expire) : 0 ;
}

void
engine_port_log (int inst, const char *format_str, va_list  args)
{
    svc_logger_vlog_state (inst, format_str, args) ;
}

void
engine_port_assert (const char *msg)
{
    dbg_assert(msg)  ;
}



int32_t
engine_port_variable_write (uint32_t idx, int32_t val)
{
    return backupreg_write32(idx + BACKUPREG_IDX_STATEMACHINE_START, (uint32_t)val) ;
}

int32_t
engine_port_variable_read (uint32_t idx, int32_t * val)
{
    return backupreg_read32(idx + BACKUPREG_IDX_STATEMACHINE_START, (uint32_t*)val) ;
}

static int32_t
_corshell_out(void* ctx, uint32_t out, const char* str)
{
    if (out >= CORSHELL_OUT_ERR) {
        DBG_MESSAGE_T (DBG_MESSAGE_SEVERITY_ERROR, 0,
                "ENG  : :  %s", str ? str : "") ;

    }

    return  ENGINE_OK ;
}


int32_t
engine_port_shellcmd (const char* shellcmd)
{
    int32_t res = ENGINE_FAIL ;
    char    cmd[128]     ;
    char *argv[CORSHELL_ARGC_MAX];
    int argc ;

    if (shellcmd) {

        int len = strlen (shellcmd) ;
        if (len > 127) len = 127 ;
        strncpy (cmd, shellcmd, len) ;
        cmd[len] = '\0' ;
        argc = corshell_cmd_split(cmd, len, /*SIPSHELL_CMD_PREFIX*/0, argv, CORSHELL_ARGC_MAX-1);

        if (argc > 0) {
            res = corshell_cmd_run (0, 0, _corshell_out, &argv[0],  argc-0) ;

        }

    }

    return res ;
}

uint32_t
engine_timestamp (void)
{
    return os_sys_timestamp () ;

}

#if CFG_UTILS_STRSUB
int32_t
engine_strsub_cb (STRSUB_REPLACE_CB cb, const char * str, size_t len, uint32_t offset, uint32_t arg)
{
    int32_t res = E_INVAL ;
    uint32_t idx ;
    int32_t value = 0 ;
    char strvalue[16] ;

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

#if CFG_UTILS_STRSUB
static int32_t
parse_strsub_cb(STRSUB_REPLACE_CB cb, const char * str, size_t len, uint32_t offset, uint32_t arg)
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

    res = snprintf (buffer, SERVICES_STRSUB_BUFFER_LEN, "[%u]", (unsigned int)idx) ;
    res = cb (buffer, res, offset, arg) ;

    return res ;
}
#endif

const char *
engine_port_sanitize_string (const char * string, uint32_t * plen)
{
#if CFG_UTILS_STRSUB
    #pragma GCC diagnostic ignored  "-Wmissing-braces"
    STRSUB_INSTANCE_T  strsub_instance = {STRSUB_ESCAPE_TOKEN, STRSUB_HANDLERS_TOKENS, {0}} ;
    STRSUB_HANDLER_T    strsub ;
    strsub_install_handler(&strsub_instance, StrsubToken1, &strsub, parse_strsub_cb) ;
    uint32_t dstlen = strsub_parse_get_dst_length (&strsub_instance, string, *plen) ;
    char * newname = heap_malloc(HEAP_SPACE, dstlen) ;
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
#if CFG_UTILS_STRSUB
    heap_free (HEAP_SPACE, (void*)string) ;
#endif
}

#endif /* CFG_PORT_CORAL */
