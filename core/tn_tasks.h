/*******************************************************************************
 *
 * TNeoKernel: real-time kernel initially based on TNKernel
 *
 *    TNKernel:                  copyright © 2004, 2013 Yuri Tiomkin.
 *    PIC32-specific routines:   copyright © 2013, 2014 Anders Montonen.
 *    TNeoKernel:                copyright © 2014       Dmitry Frank.
 *
 *    TNeoKernel was born as a thorough review and re-implementation 
 *    of TNKernel. New kernel has well-formed code, bugs of ancestor are fixed
 *    as well as new features added, and it is tested carefully with unit-tests.
 *
 *    API is changed somewhat, so it's not 100% compatible with TNKernel,
 *    hence the new name: TNeoKernel.
 *
 *    Permission to use, copy, modify, and distribute this software in source
 *    and binary forms and its documentation for any purpose and without fee
 *    is hereby granted, provided that the above copyright notice appear
 *    in all copies and that both that copyright notice and this permission
 *    notice appear in supporting documentation.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE DMITRY FRANK AND CONTRIBUTORS "AS IS"
 *    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *    PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL DMITRY FRANK OR CONTRIBUTORS BE
 *    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *    THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

/**
 * \file
 *
 *   Description:   TODO
 *
 */

#ifndef _TN_TASKS_H
#define _TN_TASKS_H

/*******************************************************************************
 *    INCLUDED FILES
 ******************************************************************************/

#include "tn_list.h"
#include "tn_common.h"

#include "tn_eventgrp.h"
#include "tn_dqueue.h"
#include "tn_mem.h"


/*******************************************************************************
 *    PUBLIC TYPES
 ******************************************************************************/

/**
 * Task state
 */
enum TN_TaskState {
   /// This state may be stored in task_state only temporarily,
   /// while some system service is in progress.
   /// It should never be publicly available.
   TN_TASK_STATE_NONE         = 0,
   ///
   /// Task is ready to run (it doesn't mean that it is running at the moment)
   TN_TASK_STATE_RUNNABLE     = (1 << 0),
   ///
   /// Task is waiting
   TN_TASK_STATE_WAIT         = (1 << 1),
   ///
   /// Task is suspended (by some other task)
   TN_TASK_STATE_SUSPEND      = (1 << 2),
   ///
   /// Task was previously waiting, and after this it was suspended
   TN_TASK_STATE_WAITSUSP     = (TN_TASK_STATE_WAIT | TN_TASK_STATE_SUSPEND),
   ///
   /// Task isn't yet activated or it was terminated by `tn_task_terminate()`.
   TN_TASK_STATE_DORMANT      = (1 << 3),
};

/**
 * Options for `tn_task_create()`
 */
enum TN_TaskCreateOpt {
   ///
   /// whether task should be activated right after it is created.
   /// If this flag is not set, user must activate it manually by calling
   /// `tn_task_activate()`.
   TN_TASK_CREATE_OPT_START = (1 << 0),
   ///
   /// for internal kernel usage only: this option must be provided
   /// when creating idle task
   TN_TASK_CREATE_OPT_IDLE =  (1 << 1),
};

/**
 * Options for `tn_task_exit()`
 */
enum TN_TaskExitOpt {
   ///
   /// whether task should be deleted right after it is exited.
   /// If this flag is not set, user must either delete it manually by
   /// calling `tn_task_delete()` or re-activate it by calling
   /// `tn_task_activate()`.
   TN_TASK_EXIT_OPT_DELETE = (1 << 0),
};

/**
 * Task
 */
struct TN_Task {
   /// pointer to task's current stack pointer;
   /// Note that this field **must** be a first field in the struct,
   /// this fact is exploited by platform-specific routines.
   unsigned int *task_stk;   
   ///
   /// queue is used to include task in ready/wait lists
   struct TN_ListItem task_queue;     
   ///
   /// queue is used to include task in timer list
   struct TN_ListItem timer_queue;
   ///
   /// pointer to object's (semaphore, mutex, event, etc) wait list in which 
   /// task is included for waiting
   struct TN_ListItem *pwait_queue;
   ///
   /// queue is used to include task in creation list
   /// (currently, this list is used for statistics only)
   struct TN_ListItem create_queue;

#if TN_USE_MUTEXES
   ///
   /// list of all mutexes that are locked by task
   struct TN_ListItem mutex_queue;
#if TN_MUTEX_DEADLOCK_DETECT
   ///
   /// list of other tasks involved in deadlock. This list is non-empty
   /// only in emergency cases, and it is here to help you fix your bug
   /// that led to deadlock.
   ///
   /// @see `TN_MUTEX_DEADLOCK_DETECT`
   struct TN_ListItem deadlock_list;
#endif
#endif

   /// base address of task's stack space
   unsigned int *stk_start;
   ///
   /// size of task's stack (in `sizeof(unsigned int)`, not bytes)
   int stk_size;
   ///
   /// pointer to task's body function given to `tn_task_create()`
   void *task_func_addr;
   ///
   /// pointer to task's parameter given to `tn_task_create()`
   void *task_func_param;
   ///
   /// base priority of the task (actual current priority may be higher than 
   /// base priority because of mutex)
   int base_priority;
   ///
   /// current task priority
   int priority;
   ///
   /// id for object validity verification
   enum TN_ObjId id_task;
   ///
   /// task state
   enum TN_TaskState task_state;
   ///
   /// reason for waiting (relevant if only `task_state` is `WAIT` or
   /// `WAITSUSP`)
   enum TN_WaitReason task_wait_reason;
   ///
   /// waiting result code (reason why waiting finished)
   enum TN_Retval task_wait_rc;
   ///
   /// remaining time until timeout; may be `TN_WAIT_INFINITE`.
   unsigned long tick_count;
   ///
   /// time slice counter
   int tslice_count;
   ///
   /// subsystem-specific fields that are used while task waits for something.
   /// Do note that these fields are grouped by union, so, they must not
   /// interfere with each other. It's quite ok here because task can't wait
   /// for different things.
   union {
#if  TN_USE_EVENTS
      /// fields specific to tn_eventgrp.h
      struct TN_EGrpTaskWait eventgrp;
#endif
      ///
      /// fields specific to tn_dqueue.h
      struct TN_DQueueTaskWait dqueue;
      ///
      /// fields specific to tn_mem.h
      struct TN_FMemTaskWait fmem;
   } subsys_wait;

#if TN_DEBUG
   /// task name for debug purposes, user may want to set it by hand
   const char *name;          
#endif

   //-- for the comments on the flag priority_already_updated,
   //   see file tn_mutex.c , function _mutex_do_unlock().
   unsigned          priority_already_updated : 1;


// Other implementation specific fields may be added below

};




/*******************************************************************************
 *    GLOBAL VARIABLES
 ******************************************************************************/

/*******************************************************************************
 *    DEFINITIONS
 ******************************************************************************/


#define get_task_by_tsk_queue(que)                                   \
   (que ? container_of(que, struct TN_Task, task_queue) : 0)

#define get_task_by_timer_queque(que)                                \
   (que ? container_of(que, struct TN_Task, timer_queue) : 0)

#define get_task_by_block_queque(que)                                \
   (que ? container_of(que, struct TN_Task, block_queue) : 0)




/*******************************************************************************
 *    PUBLIC FUNCTION PROTOTYPES
 ******************************************************************************/

/**
 * Create task.
 *
 * This function creates a task. A field id_task of the structure task must be set to 0 before invoking this
 * function. A memory for the task TCB and a task stack must be allocated before the function call. An
 * allocation may be static (global variables of the struct TN_Task type for the task and
 * unsigned int [task_stack_size] for the task stack) or dynamic, if the user application supports
 * malloc/alloc (TNKernel itself does not use dynamic memory allocation).
 * The task_stack_size value must to be chosen big enough to fit the task_func local variables and its switch
 * context (processor registers, stack and instruction pointers, etc.).
 * The task stack must to be created as an array of unsigned int. Actually, the size of stack array element
 * must be identical to the processor register size (for most 32-bits and 16-bits processors a register size
 * equals sizeof(int)).
 * A parameter task_stack_start must point to the stack bottom. For instance, if the processor stack grows
 * from the high to the low memory addresses and the task stack array is defined as
 * unsigned int xxx_xxx[task_stack_size] (in C-language notation),
 * then the task_stack_start parameter has to be &xxx_xxx[task_stack_size - 1].
 *
 * @param task       Ready-allocated struct TN_Task structure. A field id_task of it must be 
 *                   set to 0 before invocation of tn_task_create().
 * @param task_func  Task body function.
 * @param priority   Priority for new task. NOTE: the lower value, the higher priority.
 *                   Must be > 0 and < (TN_NUM_PRIORITY - 1).
 * @param task_stack_start    Task start pointer.
 *                            A task must be created as an array of int. Actually, the size
 *                            of stack array element must be identical to the processor 
 *                            register size (for most 32-bits and 16-bits processors
 *                            a register size equals sizeof(int)).
 *                            NOTE: See option TN_API_TASK_CREATE for further details.
 * @param task_stack_size     Size of task stack
 *                            (actually, size of array that is used for task_stack_start).
 * @param            Parameter that is passed to task_func.
 * @option           (0): task is created in DORMANT state,
 *                        you need to call tn_task_activate() after task creation.
 *                   (TN_TASK_START_ON_CREATION): task is created and activated.
 *                   
 */
enum TN_Retval tn_task_create(struct TN_Task *task,                  //-- task TCB
                 void (*task_func)(void *param),  //-- task function
                 int priority,                    //-- task priority
                 unsigned int *task_stack_start,  //-- task stack first addr in memory (see option TN_API_TASK_CREATE)
                 int task_stack_size,             //-- task stack size (in sizeof(void*),not bytes)
                 void *param,                     //-- task function parameter
                 enum TN_TaskCreateOpt opts       //-- creation options
                 );


/**
 * If the task is runnable, it is moved to the SUSPENDED state. If the task
 * is in the WAITING state, it is moved to the WAITING­SUSPENDED state.
 */
enum TN_Retval tn_task_suspend(struct TN_Task *task);

/**
 * Release task from SUSPENDED state. If the given task is in the SUSPENDED state,
 * it is moved to READY state; afterwards it has the lowest precedence amoung
 * runnable tasks with the same priority. If the task is in WAITING_SUSPENDED state,
 * it is moved to WAITING state.
 */
enum TN_Retval tn_task_resume(struct TN_Task *task);

/**
 * Put current task to sleep for at most timeout ticks. When the timeout
 * expires and the task was not suspended during the sleep, it is switched
 * to runnable state. If the timeout value is TN_WAIT_INFINITE and the task
 * was not suspended during the sleep, the task will sleep until another
 * function call (like tn_task_wakeup() or similar) will make it runnable.
 *
 * Each task has a wakeup request counter. If its value for currently
 * running task is greater then 0, the counter is decremented by 1 and the
 * currently running task is not switched to the sleeping mode and
 * continues execution.
 */
enum TN_Retval tn_task_sleep(unsigned long timeout);

/**
 * Wake up task from sleep.
 *
 * These functions wakes up the task specified by the task from sleep mode.
 * The function placing the task into the sleep mode will return to the task
 * without errors.
 */
enum TN_Retval tn_task_wakeup(struct TN_Task *task);
enum TN_Retval tn_task_iwakeup(struct TN_Task *task);

/**
 * Activate task that was created by tn_task_create() without TN_TASK_START_ON_CREATION
 * option.
 *
 * Task is moved from DORMANT state to the READY state.
 */
enum TN_Retval tn_task_activate(struct TN_Task *task);
enum TN_Retval tn_task_iactivate(struct TN_Task *task);

/**
 * Release task from WAIT state.
 *
 * These functions forcibly release task from any waiting state.
 * If task is in WAITING state, it is moved to READY state.
 * If task is in WAITING_SUSPENDED state, it is moved to SUSPENDED state.
 */
enum TN_Retval tn_task_release_wait(struct TN_Task *task);
enum TN_Retval tn_task_irelease_wait(struct TN_Task *task);

/**
 * This function terminates the currently running task. The task is moved to the DORMANT state.
 * If activate requests exist (activation request count is 1) the count 
 * is decremented and the task is moved to the READY state.
 * In this case the task starts execution from the beginning (as after creation and activation),
 * all mutexes locked by the task are unlocked etc.
 * The task will have the lowest precedence among all tasks with the same priority in the READY state.
 *
 * After exiting, the task may be reactivated by the tn_task_iactivate()
 * function or the tn_task_activate() function call.
 * In this case task starts execution from beginning (as after creation/activation).
 * The task will have the lowest precedence among all tasks with the same
 * priority in the READY state.
 *
 * If this function is invoked with TN_TASK_EXIT_OPT_DELETE parameter value, the task will be deleted
 * after termination and cannot be reactivated (needs recreation).
 * 
 * This function cannot be invoked from interrupts.
 */
void tn_task_exit(enum TN_TaskExitOpt opts);


/**
 * This function terminates the task specified by the task. The task is moved to the `DORMANT` state.
 * When the task is waiting in a wait queue, the task is removed from the queue.
 * If activate requests exist (activation request count is 1) the count 
 * is decremented and the task is moved to the READY state.
 * In this case the task starts execution from beginning (as after creation and activation), all
 * mutexes locked by the task are unlocked etc.
 * The task will have the lowest precedence among all tasks with the same priority in the READY state.
 *
 * After termination, the task may be reactivated by the tn_task_iactivate()
 * function or the tn_task_activate() function call.
 * In this case the task starts execution from the beginning (as after creation/activation).
 * The task will have the lowest precedence among all tasks with the same
 * priority in the READY state.
 *
 * A task must not terminate itself by this function (use the tn_task_exit() function instead).
 * This function cannot be used in interrupts.
 *
 * @see enum TN_TaskState
 */
enum TN_Retval tn_task_terminate(struct TN_Task *task);

/**
 * This function deletes the task specified by the task. The task must be in the `DORMANT` state,
 * otherwise TERR_WCONTEXT will be returned.
 *
 * This function resets the id_task field in the task structure to 0
 * and removes the task from the system tasks list.
 * The task can not be reactivated after this function call (the task must be recreated).
 *
 * This function cannot be invoked from interrupts.
 */
enum TN_Retval tn_task_delete(struct TN_Task *task);

/**
 * Set new priority for task.
 * If priority is 0, then task's base_priority is set.
 */
enum TN_Retval tn_task_change_priority(struct TN_Task *task, int new_priority);

#endif // _TN_TASKS_H


/*******************************************************************************
 *    end of file
 ******************************************************************************/


