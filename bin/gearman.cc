/* Gearman server and library
 * Copyright (C) 2008 Brian Aker, Eric Day
 * All rights reserved.
 *
 * Use and distribution licensed under the BSD license.  See
 * the COPYING file in the parent directory for full text.
 */

/**
 * @file
 * @brief Gearman Command Line Tool
 */

#include <config.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <vector>


#include <libgearman/gearman.h>

#include "bin/arguments.h"
#include "bin/client.h"
#include "bin/worker.h"
#include "util/pidfile.hpp"
#include "bin/error.h"

using namespace gearman_client;
using namespace datadifferential;

#define GEARMAN_INITIAL_WORKLOAD_SIZE 8192

struct worker_argument_t
{
  Args &args;
  Function &function;

  worker_argument_t(Args &args_arg, Function &function_arg) :
    args(args_arg),
    function(function_arg)
  {
  }
};

/**
 * Function to run in client mode.
 */
static void _client(Args &args);

/**
 * Run client jobs.
 */
static void _client_run(gearman_client_st& client, Args &args,
                        const void *workload, size_t workload_size);

/**
 * Client data/complete callback function.
 */
static gearman_return_t _client_data(gearman_task_st *task);

/**
 * Client warning/exception callback function.
 */
static gearman_return_t _client_warning(gearman_task_st *task);

/**
 * Client status callback function.
 */
static gearman_return_t _client_status(gearman_task_st *task);
/**
 * Client fail callback function.
 */
static gearman_return_t _client_fail(gearman_task_st *task);

/**
 * Function to run in worker mode.
 */
static void _worker(Args &args);

/**
 * Callback function when worker gets a job.
 */
static void *_worker_cb(gearman_job_st *job, void *context,
                        size_t *result_size, gearman_return_t *ret_ptr);

/**
 * Read workload chunk from a file descriptor and put into allocated memory.
 */
static void _read_workload(int fd, Bytes& workload);

/**
 * Print usage information.
 */
static void usage(char *name);

extern "C"
{

static void signal_setup()
{
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
  {
    error::perror("signal");
  }
}

}

int main(int argc, char *argv[])
{
  Args args(argc, argv);
  bool close_stdio= false;

  if (args.usage())
  {
    usage(argv[0]);
    return EXIT_SUCCESS;
  }

  if (args.is_error())
  {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  signal_setup();

  if (args.daemon())
  {
    switch (fork())
    {
    case -1:
      error::perror("fork");
      return EXIT_FAILURE;

    case 0:
      break;

    default:
      return EXIT_SUCCESS;
    }

    if (setsid() == -1)
    {
      error::perror("setsid");
      return EXIT_FAILURE;
    }

    close_stdio= true;
  }

  if (close_stdio)
  {
    /* If we can't remap stdio, it should not a fatal error. */
    int fd= open("/dev/null", O_RDWR, 0);

    if (fd != -1)
    {
      if (dup2(fd, STDIN_FILENO) == -1)
      {
        error::perror("dup2");
        return EXIT_FAILURE;
      }

      if (dup2(fd, STDOUT_FILENO) == -1)
      {
        error::perror("dup2");
        return EXIT_FAILURE;
      }

      if (dup2(fd, STDERR_FILENO) == -1)
      {
        error::perror("dup2");
        return EXIT_FAILURE;
      }

      close(fd);
    }
  }

  util::Pidfile _pid_file(args.pid_file());

  if (not _pid_file.create())
  {
    error::perror(_pid_file.error_message().c_str());
    return EXIT_FAILURE;
  }

  if (args.worker())
  {
    _worker(args);
  }
  else
  {
    _client(args);
  }

  return args.error();
}

void _client(Args &args)
{
  Client local_client;
  gearman_client_st &client= local_client.client();
  Bytes workload;
  if (args.timeout() >= 0)
  {
    gearman_client_set_timeout(&client, args.timeout());
  }

  if (gearman_failed(gearman_client_add_server(&client, args.host(), args.port())))
  {
    error::message("gearman_client_add_server", client);
    return;
  }

  gearman_client_set_data_fn(&client, _client_data);
  gearman_client_set_warning_fn(&client, _client_warning);
  gearman_client_set_status_fn(&client, _client_status);
  gearman_client_set_complete_fn(&client, _client_data);
  gearman_client_set_exception_fn(&client, _client_warning);
  gearman_client_set_fail_fn(&client, _client_fail);

  if (not args.arguments())
  {
    if (args.suppress_input())
    {
      _client_run(client, args, NULL, 0);
    }
    else if (args.job_per_newline())
    {
      workload.resize(GEARMAN_INITIAL_WORKLOAD_SIZE);

      while (1)
      {
        if (fgets(&workload[0], static_cast<int>(workload.size()), stdin) == NULL)
        {
          break;
        }

        if (args.strip_newline())
        {
          _client_run(client, args, &workload[0], strlen(&workload[0]) - 1);
        }
        else
        {
          _client_run(client, args, &workload[0], strlen(&workload[0]));
        }
      }
    }
    else
    {
      _read_workload(STDIN_FILENO, workload);
      _client_run(client, args, &workload[0], workload.size());
    }
  }
  else
  {
    for (size_t x= 0; args.argument(x) != NULL; x++)
    {
      _client_run(client, args, args.argument(x), strlen(args.argument(x)));
    }
  }
}

void _client_run(gearman_client_st& client, Args &args,
                 const void *workload, size_t workload_size)
{
  gearman_return_t ret;

  for (Function::vector::iterator iter= args.begin(); 
       iter != args.end();
       ++iter)
  {
    Function &function= *iter;

    /* This is a bit nasty, but all we have currently is multiple function
       calls. */
    if (args.background())
    {
      switch (args.priority())
      {
      case GEARMAN_JOB_PRIORITY_HIGH:
        (void)gearman_client_add_task_high_background(&client,
                                                      NULL,
                                                      &args,
                                                      function.name(),
                                                      args.unique(),
                                                      workload,
                                                      workload_size, &ret);
        break;

      case GEARMAN_JOB_PRIORITY_NORMAL:
        (void)gearman_client_add_task_background(&client,
                                                 NULL,
                                                 &args,
                                                 function.name(),
                                                 args.unique(),
                                                 workload,
                                                 workload_size, &ret);
        break;

      case GEARMAN_JOB_PRIORITY_LOW:
        (void)gearman_client_add_task_low_background(&client,
                                                     NULL,
                                                     &args,
                                                     function.name(),
                                                     args.unique(),
                                                     workload,
                                                     workload_size, &ret);
        break;

      case GEARMAN_JOB_PRIORITY_MAX:
      default:
        /* This should never happen. */
        ret= GEARMAN_UNKNOWN_STATE;
        break;
      }
    }
    else
    {
      switch (args.priority())
      {
      case GEARMAN_JOB_PRIORITY_HIGH:
        (void)gearman_client_add_task_high(&client,
                                           NULL,
                                           &args,
                                           function.name(),
                                           args.unique(),
                                           workload, workload_size, &ret);
        break;

      case GEARMAN_JOB_PRIORITY_NORMAL:
        (void)gearman_client_add_task(&client,
                                      NULL,
                                      &args,
                                      function.name(),
                                      args.unique(),
                                      workload,
                                      workload_size, &ret);
        break;

      case GEARMAN_JOB_PRIORITY_LOW:
        (void)gearman_client_add_task_low(&client,
                                          NULL,
                                          &args,
                                          function.name(),
                                          args.unique(),
                                          workload, workload_size, &ret);
        break;

      case GEARMAN_JOB_PRIORITY_MAX:
      default:
        /* This should never happen. */
        ret= GEARMAN_UNKNOWN_STATE;
        break;
      }
    }

    if (gearman_failed(ret))
    {
      error::message("gearman_client_add_task", client);
    }
  }

  if (gearman_failed(gearman_client_run_tasks(&client)))
  {
    error::message("gearman_client_run_tasks", client);
  }
}

static gearman_return_t _client_data(gearman_task_st *task)
{
  const Args *args= static_cast<const Args*>(gearman_task_context(task));
  if (args->prefix())
  {
    fprintf(stdout, "%s: ", gearman_task_function_name(task));
    fflush(stdout);
  }

  if (write(fileno(stdout), gearman_task_data(task), gearman_task_data_size(task)) == -1)
  {
    error::perror("write");
    return GEARMAN_ERRNO;
  }

  return GEARMAN_SUCCESS;
}

static gearman_return_t _client_warning(gearman_task_st *task)
{
  const Args *args= static_cast<const Args*>(gearman_task_context(task));
  if (args->prefix())
  {
    fprintf(stderr, "%s: ", gearman_task_function_name(task));
    fflush(stderr);
  }

  if (write(fileno(stderr), gearman_task_data(task), gearman_task_data_size(task)) == -1)
  {
    error::perror("write");
  }

  return GEARMAN_SUCCESS;
}

static gearman_return_t _client_status(gearman_task_st *task)
{
  const Args *args= static_cast<const Args*>(gearman_task_context(task));

  if (args->prefix())
    printf("%s: ", gearman_task_function_name(task));

  printf("%u%% Complete\n", (gearman_task_numerator(task) * 100) /
         gearman_task_denominator(task));

  return GEARMAN_SUCCESS;
}

static gearman_return_t _client_fail(gearman_task_st *task)
{
  const Args *args= static_cast<const Args *>(gearman_task_context(task));

  if (args->prefix())
    fprintf(stderr, "%s: ", gearman_task_function_name(task));

  fprintf(stderr, "Job failed\n");

  args->set_error();

  return GEARMAN_SUCCESS;
}

static void _worker_free(void *, void *)
{
}

void _worker(Args &args)
{
  Worker local_worker;
  gearman_worker_st &worker= local_worker.worker();

  if (args.timeout() >= 0)
  {
    gearman_worker_set_timeout(&worker, args.timeout());
  }

  if (gearman_failed(gearman_worker_add_server(&worker, args.host(), args.port())))
  {
    error::message("gearman_worker_add_server", worker);
    _exit(EXIT_FAILURE);
  }

  gearman_worker_set_workload_free_fn(&worker, _worker_free, NULL);

  for (Function::vector::iterator iter= args.begin(); 
       iter != args.end();
       ++iter)
  {
    Function &function= *iter;
    worker_argument_t pass(args, *iter);
    if (gearman_failed(gearman_worker_add_function(&worker, function.name(), 0, _worker_cb, &pass)))
    {
      error::message("gearman_worker_add_function", worker);
      _exit(EXIT_FAILURE);
    }
  }

  while (1)
  {
    if (gearman_failed(gearman_worker_work(&worker)))
    {
      error::message("gearman_worker_work", worker);
    }

    if (args.count() > 0)
    {
      --args.count();
      if (args.count() == 0)
        break;
    }
  }
}

extern "C" {
static bool local_wexitstatus(int status)
{
  if (WEXITSTATUS(status) != 0)
    return true;

  return false;
}
}

static void *_worker_cb(gearman_job_st *job, void *context,
                        size_t *result_size, gearman_return_t *ret_ptr)
{
  worker_argument_t *arguments= static_cast<worker_argument_t *>(context);

  Args &args= arguments->args;
  Function &function= arguments->function;

  function.buffer().clear();

  *ret_ptr= GEARMAN_SUCCESS;

  if (not args.arguments())
  {
    if (write(STDOUT_FILENO, gearman_job_workload(job),
              gearman_job_workload_size(job)) == -1)
    {
      error::perror("write");
    }
  }
  else
  {
    int in_fds[2];
    int out_fds[2];

    if (pipe(in_fds) == -1 or pipe(out_fds) == -1)
    {
      error::perror("pipe");
    }

    pid_t pid;
    switch ((pid= fork()))
    {
    case -1:
      error::perror("fork");
      return NULL;

    case 0:
      if (dup2(in_fds[0], 0) == -1)
      {
        error::perror("dup2");
        return NULL;
      }

      if (close(in_fds[1]) < 0)
      {
        error::perror("close");
        return NULL;
      }

      if (dup2(out_fds[1], 1) == -1)
      {
        error::perror("dup2");
        return NULL;
      }

      if (close(out_fds[0]) < 0)
      {
        error::perror("close");
        return NULL;
      }

      if (execvp(args.argument(0), args.argumentv()) < 0)
      {
        error::perror("execvp");
        return NULL;
      }

    default:
      break;
    }

    if (close(in_fds[0]) < 0)
    {
      error::perror("close");
    }

    if (close(out_fds[1]) < 0)
    {
      error::perror("close");
    }

    if (gearman_job_workload_size(job) > 0)
    {
      if (write(in_fds[1], gearman_job_workload(job),
                gearman_job_workload_size(job)) == -1)
      {
        error::perror("write");
      }
    }

    if (close(in_fds[1]) < 0)
    {
      error::perror("close");
    }

    if (args.job_per_newline())
    {
      FILE *f= fdopen(out_fds[0], "r");

      if (f == NULL)
      {
        error::perror("fdopen");
      }

      function.buffer().clear();

      while (1)
      {
        char buffer[1024];
        if (fgets(buffer, sizeof(buffer), f) == NULL)
        {
          break;
        }

        size_t length= strlen(buffer);
        for (size_t x= 0; x < length ; x++)
        {
          function.buffer().push_back(buffer[x]);
        }

        if (args.strip_newline())
        {
          *ret_ptr= gearman_job_send_data(job, function.buffer_ptr(), function.buffer().size() - 1);
        }
        else
        {
          *ret_ptr= gearman_job_send_data(job, function.buffer_ptr(), function.buffer().size());
        }

        if (*ret_ptr != GEARMAN_SUCCESS)
        {
          error::message("gearman_job_send_data() failed with", *ret_ptr);
          break;
        }
      }

      function.buffer().clear();
      fclose(f);
    }
    else
    {
      _read_workload(out_fds[0], function.buffer());
      if (close(out_fds[0]) < 0)
      {
        error::perror("close");
      }
      *result_size= function.buffer().size();
    }

    int status;
    if (wait(&status) == -1)
    {
      error::perror("wait");
    }

    if (local_wexitstatus(status))
    {
      if (not function.buffer().empty())
      {
        *ret_ptr= gearman_job_send_data(job, function.buffer_ptr(), function.buffer().size());
        if (*ret_ptr != GEARMAN_SUCCESS)
          return NULL;
      }

      *ret_ptr= GEARMAN_WORK_FAIL;
      return NULL;
    }
  }

  return function.buffer_ptr();
}

void _read_workload(int fd, Bytes& workload)
{
  while (1)
  {
    char buffer[1024];

    ssize_t read_ret= read(fd, buffer, sizeof(buffer));

    if (read_ret == -1)
    {
      error::perror("read");
    }
    else if (read_ret == 0)
    {
      break;
    }

    workload.reserve(workload.size() + static_cast<size_t>(read_ret));
    for (size_t x= 0; x < static_cast<size_t>(read_ret); x++)
    {
      workload.push_back(buffer[x]);
    }
  }
}

static void usage(char *name)
{
  printf("Client mode: %s [options] [<data>]\n", name);
  printf("Worker mode: %s -w [options] [<command> [<args> ...]]\n", name);

  printf("\nCommon options to both client and worker modes.\n");
  printf("\t-f <function> - Function name to use for jobs (can give many)\n");
  printf("\t-h <host>     - Job server host\n");
  printf("\t-H            - Print this help menu\n");
  printf("\t-p <port>     - Job server port\n");
  printf("\t-t <timeout>  - Timeout in milliseconds\n");
  printf("\t-i <pidfile>  - Create a pidfile for the process\n");

  printf("\nClient options:\n");
  printf("\t-b            - Run jobs in the background\n");
  printf("\t-I            - Run jobs as high priority\n");
  printf("\t-L            - Run jobs as low priority\n");
  printf("\t-n            - Run one job per line\n");
  printf("\t-N            - Same as -n, but strip off the newline\n");
  printf("\t-P            - Prefix all output lines with functions names\n");
  printf("\t-s            - Send job without reading from standard input\n");
  printf("\t-u <unique>   - Unique key to use for job\n");

  printf("\nWorker options:\n");
  printf("\t-c <count>    - Number of jobs for worker to run before exiting\n");
  printf("\t-n            - Send data packet for each line\n");
  printf("\t-N            - Same as -n, but strip off the newline\n");
  printf("\t-w            - Run in worker mode\n");
}
