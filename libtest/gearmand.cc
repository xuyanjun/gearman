/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Data Differential YATL (i.e. libtest)  library
 *
 *  Copyright (C) 2012 Data Differential, http://datadifferential.com/
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

#include <config.h>
#include <libtest/common.h>

#include <libtest/gearmand.h>

#include "util/instance.hpp"
#include "util/operation.hpp"

using namespace datadifferential;
using namespace libtest;

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libgearman/gearman.h>

#ifndef __INTEL_COMPILER
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

using namespace libtest;

class Gearmand : public libtest::Server
{
private:
public:
  Gearmand(const std::string& host_arg, in_port_t port_arg) :
    libtest::Server(host_arg, port_arg, GEARMAND_BINARY, true)
  {
    set_pid_file();
  }

  bool ping()
  {
    gearman_client_st *client= gearman_client_create(NULL);
    if (client == NULL)
    {
      Error << "Could not allocate memory for gearman_client_create()";
      return false;
    }
    gearman_client_set_timeout(client, 3000);

    if (gearman_success(gearman_client_add_server(client, hostname().c_str(), port())))
    {
      gearman_return_t rc= gearman_client_echo(client, test_literal_param("This is my echo test"));

      if (gearman_success(rc))
      {
        gearman_client_free(client);
        return true;
      }
      
      if (out_of_ban_killed() == false)
      {
        Error << hostname().c_str() << ":" << port() << " was " << gearman_strerror(rc) << " extended: " << gearman_client_error(client);
      }
    }
    else
    {
      Error << "gearman_client_add_server() " << gearman_client_error(client);
    }

    gearman_client_free(client);

    return false;;
  }

  const char *name()
  {
    return "gearmand";
  };

  void log_file_option(Application& app, const std::string& arg)
  {
    if (arg.empty() == false)
    {
      std::string buffer("--log-file=");
      buffer+= arg;
      app.add_option("--verbose=DEBUG");
      app.add_option(buffer);
    }
  }

  bool has_log_file_option() const
  {
    return true;
  }

  bool is_libtool()
  {
    return true;
  }

  bool has_syslog() const
  {
    return false; //  --syslog.errmsg-enable
  }

  bool has_port_option() const
  {
    return true;
  }

  bool build(size_t argc, const char *argv[]);
};

bool Gearmand::build(size_t argc, const char *argv[])
{
  if (getuid() == 0 or geteuid() == 0)
  {
    add_option("-u", "root");
  }

  add_option("--listen=localhost");

  for (size_t x= 0 ; x < argc ; x++)
  {
    add_option(argv[x]);
  }

  return true;
}

namespace libtest {

libtest::Server *build_gearmand(const char *hostname, in_port_t try_port)
{
  return new Gearmand(hostname, try_port);
}

}
