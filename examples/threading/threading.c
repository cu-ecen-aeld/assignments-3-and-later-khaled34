#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define DEBUG_ENABLE        1

#if (DEBUG_ENABLE)
#define DEBUG_LOG(msg,...) printf("threading: [%s]: " msg "\n" , __func__,##__VA_ARGS__)
#else
#define DEBUG_LOG(msg,...)
#endif /*DEBUG_ENABLE*/
#define ERROR_LOG(msg,...) printf("threading ERROR: [%s]: " msg "\n" ,__func__, ##__VA_ARGS__)

#define FROM_MS_TO_US(mtime)        ((mtime) * 1000)
#define INIT_THREAD_PARAM(mutex, wait_to_obtain_ms, wait_to_release_ms)                             \
                                        {                                                           \
                                            DEBUG_LOG("Setup Thread parameters\n");                 \
                                            ptr_thread_params->mutex = mutex;                           \
                                            ptr_thread_params->wait_to_obtain_ms  = wait_to_obtain_ms;  \
                                            ptr_thread_params->wait_to_release_ms = wait_to_release_ms; \
                                            ptr_thread_params->thread_complete_success = false;         \
                                        }
void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    pthread_t threadId;
    bool retval; 

    threadId = pthread_self();
    thread_func_args->threadId = threadId;
    
    DEBUG_LOG("From ThreadID %ld: waiting before obtaining mutex ...\n",threadId);
    retval = usleep(FROM_MS_TO_US(thread_func_args->wait_to_obtain_ms));
    if (EXIT_SUCCESS != retval)
    {
        ERROR_LOG("usleep with wait to obtain issue\n");
        goto func_exit;
    }

    DEBUG_LOG("From ThreadID %ld: Obtaining the mutex ...\n", threadId);
    retval = pthread_mutex_lock(thread_func_args->mutex);
    if (EXIT_SUCCESS != retval)
    {
        ERROR_LOG("issue in obtaining the mutex\n");
        goto func_exit;
    }

    DEBUG_LOG("From ThreadID %ld: waiting before releasing mutex ...\n", threadId);
    retval = usleep(FROM_MS_TO_US(thread_func_args->wait_to_release_ms));
    if (EXIT_SUCCESS != retval)
    {
        ERROR_LOG("usleep with wait to release issue\n");
        goto func_exit;
    }


    DEBUG_LOG("From ThreadID %ld: releasing the mutex ...\n", threadId);
    retval = pthread_mutex_unlock(thread_func_args->mutex);
    if (EXIT_SUCCESS != retval)
    {
        ERROR_LOG("issue in obtaining the mutex\n");
        goto func_exit;
    }

func_exit:
    thread_func_args->thread_complete_success = (retval == EXIT_SUCCESS)? true:false;
    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    struct thread_data* ptr_thread_params = NULL;
    bool status = false;
    int retval;

    DEBUG_LOG("Allocation of Thread data\n");
    ptr_thread_params = (struct thread_data* ) malloc (sizeof(struct thread_data));
    if (ptr_thread_params == NULL)
    {
        ERROR_LOG("Thread data allocation: Memory capacity exceeded\n");
        goto func_exit;
    }
    
    /* Setup Thread Params */
    INIT_THREAD_PARAM(mutex, wait_to_obtain_ms, wait_to_release_ms);

    DEBUG_LOG("Creating the thread and call its entry point\n");
    /* Create the thread */
    retval = pthread_create(thread, NULL, threadfunc, (void*)(ptr_thread_params));
    if (EXIT_SUCCESS != retval)
    {
        ERROR_LOG("Thread Creation Failed: Error code is %d\n", retval);
        goto func_exit;
    }
    else
    {
        DEBUG_LOG("Thread Creation Passed and the thread id created is %ld!\n", *thread);
        status = true;
    }
    /* The start_thread_obtaining_mutex function should only start the thread and should not block 
       for the thread to complete. 
       So no need to join here*/
    
func_exit:
    return status;
}

