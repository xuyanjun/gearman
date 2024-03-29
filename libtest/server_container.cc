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

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <iostream>

#include <algorithm> 
#include <functional> 
#include <locale>

// trim from end 
static inline std::string &rtrim(std::string &s)
{ 
  s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end()); 
  return s; 
}

namespace libtest {

void server_startup_st::push_server(Server *arg)
{
  servers.push_back(arg);

  std::string server_config_string;
  if (arg->has_socket())
  {
    server_config_string+= "--socket=";
    server_config_string+= '"';
    server_config_string+= arg->socket();
    server_config_string+= '"';
    server_config_string+= " ";
  }
  else
  {
    char port_str[NI_MAXSERV]= { 0 };
    snprintf(port_str, sizeof(port_str), "%u", int(arg->port()));

    server_config_string+= "--server=";
    server_config_string+= arg->hostname();
    server_config_string+= ":";
    server_config_string+= port_str;
    server_config_string+= " ";
  }

  server_list+= server_config_string;

}

Server* server_startup_st::pop_server()
{
  Server *tmp= servers.back();
  servers.pop_back();
  return tmp;
}

// host_to_shutdown => host number to shutdown in array
bool server_startup_st::shutdown(uint32_t host_to_shutdown)
{
  if (servers.size() > host_to_shutdown)
  {
    Server* tmp= servers[host_to_shutdown];

    if (tmp and tmp->kill() == false)
    { }
    else
    {
      return true;
    }
  }

  return false;
}

void server_startup_st::clear()
{
  for (std::vector<Server *>::iterator iter= servers.begin(); iter != servers.end(); ++iter)
  {
    delete *iter;
  }
  servers.clear();
}

bool server_startup_st::check() const
{
  bool success= true;
  for (std::vector<Server *>::const_iterator iter= servers.begin(); iter != servers.end(); ++iter)
  {
    if ((*iter)->check()  == false)
    {
      success= false;
    }
  }

  return success;
}

bool server_startup_st::shutdown()
{
  bool success= true;
  for (std::vector<Server *>::iterator iter= servers.begin(); iter != servers.end(); ++iter)
  {
    if ((*iter)->has_pid() and (*iter)->kill() == false)
    {
      Error << "Unable to kill:" <<  *(*iter);
      success= false;
    }
  }

  return success;
}

void server_startup_st::restart()
{
  for (std::vector<Server *>::iterator iter= servers.begin(); iter != servers.end(); ++iter)
  {
    (*iter)->start();
  }
}

#define MAGIC_MEMORY 123575
server_startup_st::server_startup_st() :
  _magic(MAGIC_MEMORY),
  _socket(false),
  _sasl(false),
  _count(0),
  udp(0),
  _servers_to_run(5)
{ }

server_startup_st::~server_startup_st()
{
  clear();
}

bool server_startup_st::validate()
{
  return _magic == MAGIC_MEMORY;
}

bool server_startup(server_startup_st& construct, const std::string& server_type, in_port_t try_port, int argc, const char *argv[], const bool opt_startup_message)
{
  if (try_port <= 0)
  {
    throw libtest::fatal(LIBYATL_DEFAULT_PARAM, "was passed the invalid port number %d", int(try_port));
  }

  libtest::Server *server= NULL;
  try {
    if (0)
    { }
    else if (server_type.compare("gearmand") == 0)
    {
      if (GEARMAND_BINARY)
      {
        if (HAVE_LIBGEARMAN)
        {
          server= build_gearmand("localhost", try_port);
        }
      }
    }
    else if (server_type.compare("drizzled") == 0)
    {
      if (DRIZZLED_BINARY)
      {
        if (HAVE_LIBDRIZZLE)
        {
          server= build_drizzled("localhost", try_port);
        }
      }
    }
    else if (server_type.compare("blobslap_worker") == 0)
    {
      if (GEARMAND_BINARY)
      {
        if (GEARMAND_BLOBSLAP_WORKER)
        {
          if (HAVE_LIBGEARMAN)
          {
            server= build_blobslap_worker(try_port);
          }
        }
      }
    }
    else if (server_type.compare("memcached-sasl") == 0)
    {
      if (MEMCACHED_SASL_BINARY)
      {
        if (HAVE_LIBMEMCACHED)
        {
          server= build_memcached_sasl("localhost", try_port, construct.username(), construct.password());
        }
      }
    }
    else if (server_type.compare("memcached") == 0)
    {
      if (HAVE_MEMCACHED_BINARY)
      {
        if (HAVE_LIBMEMCACHED)
        {
          server= build_memcached("localhost", try_port);
        }
      }
    }
    else if (server_type.compare("memcached-light") == 0)
    {
      if (MEMCACHED_LIGHT_BINARY)
      {
        if (HAVE_LIBMEMCACHED)
        {
          server= build_memcached_light("localhost", try_port);
        }
      }
    }

    if (server == NULL)
    {
      throw libtest::fatal(LIBYATL_DEFAULT_PARAM, "Launching of an unknown server was attempted: %s", server_type.c_str());
    }

    /*
      We will now cycle the server we have created.
    */
    if (server->cycle() == false)
    {
      Error << "Could not start up server " << *server;
      delete server;
      return false;
    }

    server->build(argc, argv);

    if (false)
    {
      Out << "Pausing for startup, hit return when ready.";
      std::string gdb_command= server->base_command();
      std::string options;
#if 0
      Out << "run " << server->args(options);
#endif
      getchar();
    }
    else if (server->start() == false)
    {
      delete server;
      return false;
    }
    else
    {
      if (opt_startup_message)
      {
        Outn();
        Out << "STARTING SERVER(pid:" << server->pid() << "): " << server->running();
        Outn();
      }
    }
  }
  catch (...)
  {
    delete server;
    throw;
  }

  construct.push_server(server);

  return true;
}

bool server_startup_st::start_socket_server(const std::string& server_type, const in_port_t try_port, int argc, const char *argv[])
{
  (void)try_port;
  Outn();

  Server *server= NULL;
  try {
    if (0)
    { }
    else if (server_type.compare("gearmand") == 0)
    {
      Error << "Socket files are not supported for gearmand yet";
    }
    else if (server_type.compare("memcached-sasl") == 0)
    {
      if (MEMCACHED_SASL_BINARY)
      {
        if (HAVE_LIBMEMCACHED)
        {
          server= build_memcached_sasl_socket("localhost", try_port, username(), password());
        }
        else
        {
          Error << "Libmemcached was not found";
        }
      }
      else
      {
        Error << "No memcached binary is available";
      }
    }
    else if (server_type.compare("memcached") == 0)
    {
      if (MEMCACHED_BINARY)
      {
        if (HAVE_LIBMEMCACHED)
        {
          server= build_memcached_socket("localhost", try_port);
        }
        else
        {
          Error << "Libmemcached was not found";
        }
      }
      else
      {
        Error << "No memcached binary is available";
      }
    }
    else
    {
      Error << "Failed to start " << server_type << ", no support was found to be compiled in for it.";
    }

    if (server == NULL)
    {
      Error << "Failure occured while creating server: " <<  server_type;
      return false;
    }

    /*
      We will now cycle the server we have created.
    */
    if (server->cycle() == false)
    {
      Error << "Could not start up server " << *server;
      delete server;
      return false;
    }

    server->build(argc, argv);

    if (false)
    {
      Out << "Pausing for startup, hit return when ready.";
      std::string gdb_command= server->base_command();
      std::string options;
#if 0
      Out << "run " << server->args(options);
#endif
      getchar();
    }
    else if (server->start() == false)
    {
      Error << "Failed to start " << *server;
      delete server;
      return false;
    }
    else
    {
      Out << "STARTING SERVER(pid:" << server->pid() << "): " << server->running();
    }
  }
  catch (...)
  {
    delete server;
    throw;
  }

  push_server(server);

  set_default_socket(server->socket().c_str());

  Outn();

  return true;
}

std::string server_startup_st::option_string() const
{
  std::string temp= server_list;
  rtrim(temp);
  return temp;
}


} // namespace libtest
