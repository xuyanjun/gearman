/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * 
 *  Cycle the Gearmand server
 *
 *  Copyright (C) 2011 Data Differential, http://datadifferential.com/
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *      * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following disclaimer
 *  in the documentation and/or other materials provided with the
 *  distribution.
 *
 *      * The names of its contributors may not be used to endorse or
 *  promote products derived from this software without specific prior
 *  written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


/*
  Test that we are cycling the servers we are creating during testing.
*/

#include <config.h>

#include <libtest/test.hpp>
using namespace libtest;

#include <libgearman/gearman.h>


#ifndef __INTEL_COMPILER
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

#include <tests/workers.h>
#include <tests/start_worker.h>

#define WORKER_FUNCTION_NAME "echo_function"

namespace cli {

struct Context
{
  server_startup_st& servers;

  Context(server_startup_st& servers_arg) :
    servers(servers_arg)
  { }

  void push(worker_handle_st *worker_arg)
  {
    _workers.push_back(worker_arg);
  }

  void shutdown_workers()
  {
    for (std::vector<worker_handle_st *>::iterator iter= _workers.begin(); iter != _workers.end(); ++iter)
    {
      delete *iter;
    }
    _workers.clear();
  }

  ~Context()
  {
    shutdown_workers();
  }

private:
  std::vector<worker_handle_st *>_workers;

};

}

static test_return_t gearman_help_test(void *)
{
  const char *args[]= { "-H", 0 };

  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearman", args, true));
  return TEST_SUCCESS;
}

static test_return_t gearman_unknown_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "-p %d", int(default_port()));
  const char *args[]= { buffer, "--unknown", 0 };

  // The argument doesn't exist, so we should see an error
  test_compare(EXIT_FAILURE, exec_cmdline("bin/gearman", args, true));

  return TEST_SUCCESS;
}

static test_return_t gearman_client_background_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "-p %d", int(default_port()));
  const char *args[]= { buffer, "-f", WORKER_FUNCTION_NAME, "-b", "payload", 0 };

  // The argument doesn't exist, so we should see an error
  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearman", args, true));

  return TEST_SUCCESS;
}

#define REGRESSION_FUNCTION_833394 "55_char_function_name_________________________________"

static test_return_t regression_833394_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "-p %d", int(default_port()));
  const char *args[]= { buffer, "-f", REGRESSION_FUNCTION_833394, "-b", "payload", 0 };

  // The argument doesn't exist, so we should see an error
  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearman", args, true));

  return TEST_SUCCESS;
}

static test_return_t gearadmin_help_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "--port=%d", int(default_port()));
  const char *args[]= { buffer, "--help", 0 };

  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearadmin", args, true));
  return TEST_SUCCESS;
}

static test_return_t gearadmin_shutdown_test(void *object)
{
  cli::Context *context= (cli::Context*)object;

  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "--port=%d", int(default_port()));
  const char *args[]= { buffer, "--shutdown", 0 };

  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearadmin", args, true));

  Server *server= context->servers.pop_server();
  test_true(server);
  
  // We will now quiet down the false error about it not being able to restart
  server->out_of_ban_killed(true);

  while (server->ping()) 
  {
    // Wait out the death of the server
  }

  // Since we killed the server above, we need to reset it
  delete server;

  return TEST_SUCCESS;
}

static test_return_t gearadmin_version_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "--port=%d", int(default_port()));
  const char *args[]= { buffer, "--server-version", 0 };

  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearadmin", args, true));
  return TEST_SUCCESS;
}

static test_return_t gearadmin_verbose_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "--port=%d", int(default_port()));
  const char *args[]= { buffer, "--server-verbose", 0 };

  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearadmin", args, true));
  return TEST_SUCCESS;
}

static test_return_t gearadmin_status_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "--port=%d", int(default_port()));
  const char *args[]= { buffer, "--status", 0 };

  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearadmin", args, true));
  return TEST_SUCCESS;
}

static test_return_t gearadmin_workers_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "--port=%d", int(default_port()));
  const char *args[]= { buffer, "--workers", 0 };

  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearadmin", args, true));
  return TEST_SUCCESS;
}

static test_return_t gearadmin_create_drop_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "--port=%d", int(default_port()));

  const char *create_args[]= { buffer, "--create-function=test_function", 0 };
  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearadmin", create_args, true));

  const char *drop_args[]= { buffer, "--drop-function=test_function", 0 };
  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearadmin", drop_args, true));


  return TEST_SUCCESS;
}

static test_return_t gearadmin_getpid_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "--port=%d", int(default_port()));
  const char *args[]= { buffer, "--getpid", 0 };

  test_compare(EXIT_SUCCESS, exec_cmdline("bin/gearadmin", args, true));
  return TEST_SUCCESS;
}

static test_return_t gearadmin_unknown_test(void *)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "--port=%d", int(default_port()));
  const char *args[]= { buffer, "--unknown", 0 };

  // The argument doesn't exist, so we should see an error
  test_compare(EXIT_FAILURE, exec_cmdline("bin/gearadmin", args, true));

  return TEST_SUCCESS;
}

static test_return_t shutdown_workers(void *object)
{
  cli::Context *context= (cli::Context*)object;

  context->shutdown_workers();

  return TEST_SUCCESS;
}


test_st gearman_tests[] ={
  { "--help", 0, gearman_help_test },
  { "-H", 0, gearman_help_test },
  { "--unknown", 0, gearman_unknown_test },
  { "-f echo -b payload", 0, gearman_client_background_test },
  { "lp:833394", 0, regression_833394_test },
  { 0, 0, 0 }
};


test_st gearadmin_tests[] ={
  {"--help", 0, gearadmin_help_test},
  {"--server-version", 0, gearadmin_version_test},
  {"--server-verbose", 0, gearadmin_verbose_test},
  {"--status", 0, gearadmin_status_test},
  {"--getpid", 0, gearadmin_getpid_test},
  {"--workers", 0, gearadmin_workers_test},
  {"--create-function and --drop-function", 0, gearadmin_create_drop_test},
  {"--unknown", 0, gearadmin_unknown_test},
  {0, 0, 0}
};

test_st gearadmin_shutdown_tests[] ={
  {"--shutdown", 0, gearadmin_shutdown_test}, // Must be run last since it shuts down the server
  {0, 0, 0}
};

collection_st collection[] ={
  {"gearman", 0, 0, gearman_tests},
  {"gearadmin", 0, 0, gearadmin_tests},
  {"gearadmin --shutdown", shutdown_workers, 0, gearadmin_shutdown_tests},
  {0, 0, 0, 0}
};

static void *world_create(server_startup_st& servers, test_return_t& error)
{
  if (server_startup(servers, "gearmand", default_port(), 0, NULL) == false)
  {
    error= TEST_FAILURE;
    return NULL;
  }

  cli::Context *context= new cli::Context(servers);

  if (context == NULL)
  {
    error= TEST_FAILURE;
    return NULL;
  }
  
  // Echo function
  gearman_function_t echo_react_fn_v2= gearman_function_create(echo_or_react_worker_v2);
  context->push(test_worker_start(default_port(), NULL, WORKER_FUNCTION_NAME, echo_react_fn_v2, NULL, gearman_worker_options_t()));
  context->push(test_worker_start(default_port(), NULL, REGRESSION_FUNCTION_833394, echo_react_fn_v2, NULL, gearman_worker_options_t()));

  return context;
}

static bool world_destroy(void *object)
{
  cli::Context *context= (cli::Context*)object;
  delete context;

  return TEST_SUCCESS;
}


void get_world(Framework *world)
{
  world->collections(collection);
  world->create(world_create);
  world->destroy(world_destroy);
}
