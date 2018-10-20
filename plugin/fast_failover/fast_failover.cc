/* Copyright (c) 2006, 2016, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#include <my_global.h>
#include <sql_priv.h>
#include <stdlib.h>
#include <ctype.h>
#include <mysql_version.h>
#include <mysql/plugin.h>
#include <my_dir.h>
#include "my_pthread.h"                         // pthread_handler_t
#include "my_sys.h"                             // my_write, my_malloc
#include "m_string.h"                           // strlen
#include "sql_plugin.h"                         // st_plugin_int

#define FAST_FAILOVER_INTERVAL_SECS 10
#define FAST_FAILOVER_THRESHOLD 6
#define FAST_FAILOVER_COOLDOWN_SECS 600
#define FAST_FAILOVER_ON_SHUTDOWN 0

static in_addr local_service;
static char plugin_on;
static char fast_failover_on_shutdown;
int fast_failover_master_status;
unsigned int fast_failover_count;
unsigned int fast_failover_interval_secs;
unsigned int fast_failover_threshold;
unsigned int fast_failover_cooldown_secs;


static void plugin_on_set(
    THD *thd MY_ATTRIBUTE((unused)),
    struct st_mysql_sys_var *var MY_ATTRIBUTE((unused)),
    void *var_ptr MY_ATTRIBUTE((unused)),
    const void *save)
{
  if (*(my_bool*) save)
  {
    plugin_on = 1;
    my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                          "fast_failover plugin on");
  }
  else
  {
    plugin_on = 0;
    my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                          "fast_failover plugin off");
  }
}


static MYSQL_SYSVAR_BOOL(on,
                         plugin_on,
                         PLUGIN_VAR_NOCMDARG,
                         "Turns the plugin on and off.",
                         NULL,
                         plugin_on_set,
			 1);


static MYSQL_SYSVAR_BOOL(on_shutdown,
                         fast_failover_on_shutdown,
                         PLUGIN_VAR_NOCMDARG,
                         "If enabled we will perform an election on plugin deinit",
                         NULL,
			 NULL,
			 FAST_FAILOVER_ON_SHUTDOWN);


static MYSQL_SYSVAR_INT(interval_secs,
			fast_failover_interval_secs,
			PLUGIN_VAR_RQCMDARG, "Number of seconds between topology updates"
			NULL, NULL, 0, 0, INT_MAX, FAST_FAILOVER_INTERVAL_SECS);


static MYSQL_SYSVAR_INT(failover_threshold,
			fast_failover_threshold,
			PLUGIN_VAR_RQCMDARG, "Number of failures before master election"
			NULL, NULL, 0, 0, INT_MAX, FAST_FAILOVER_THRESHOLD);


static MYSQL_SYSVAR_INT(failover_cooldown,
			fast_failover_cooldown_secs,
			PLUGIN_VAR_RQCMDARG, "Number of seconds before the next master election"
			NULL, NULL, 0, 0, INT_MAX, FAST_FAILOVER_COOLDOWN_SECS);


static struct st_mysql_show_var simple_status[]=
{
  { "fast_failover_master_status",
    (char *) &fast_failover_master_status,
    SHOW_INT },
  { "fast_failover_count",
    (int *) &fast_failover_count,
    SHOW_INT },
  { 0, 0, SHOW_UNDEF }
};


int update_topology() {
  // update our service timestamp
  // if failed then stash errno in ffms
  // next check write_preferences and set
  // negative for ffms to trigger an election
  // if no promotion necessary return 0
  result = update_service();
  master_score = check_write_preferences();
  fast_failover_master_status = result or master_score
  return fast_failover_master_status
};


list<in_addr> get_services() {
  return services;
};


bool master_election_quorum() {
  // for each service in our tier
  // SHOW STATUS LIKE "fast_failover_master_status";
  // affirmative_votes = filter(statii, lambda s: s != 0)
  return false;
};


bool master_election() {
  // new_master = SELECT ip_addr, port FROM services as svcs, write_preferences as wp
  // ORDER BY wp.score ASC;
  // if (new_master == local_service) {
  //     change_master_to(max_replica)
  //     set_read_only(local_service, false)
  // change_master_to(new_master)
  return true;
};


bool change_master_to(in_addr new_master) {
  // "CHANGE MASTER TO " new_master
  return true;
};


unsigned int catchup_from_replica() {
  return 0;
};


struct databases {
  unsigned int service_id,
  my_string database_name,
  unsigned int op_mask,
};


struct service {
  service_name,
  service_id,
  ip_addr,
  port,
  op_mask,
  updated,
  deleted,
};


struct service_tier {
  char *name,
  unsigned int id,
  list<service> services,
  list<databases> databases,
};


struct fast_failover_context {
  pthread_t monitor_thread,
  MY_CONN local_service,
  service_tier tier,
};

/*
  Monitor Thread is responsible for updating topology database.
  If it fails to do so it will initiate a master_election

  SYNOPSIS
    monitor_thread()

  DESCRIPTION
    Main loop to monitor master health
*/
pthread_handler_t monitor_thread(void *p)
{
  DBUG_ENTER("Monitor Thread starting...");
  unsigned int failure_counter = 0;
  time_t failover_time_last;
  while(1) // shouldn't there be some shutdown event to wait on?
  {
    sleep(SYSVAR(interval_secs));
    if (!SYSVAR(on)) continue

    // here we handle live and dead
    // master promotions. With write
    // priorities we can move the master
    // to better services when the services
    // are healthy.
    int update_result = update_topology()
    fast_failover_master_status = update_result
    if (update_result == 0) continue // nothing to do

    failure_counter++;
    if (failure_counter > SYSVAR(failover_threshold) &&
        master_election_quorum())
    {
      if (failover_time_last - my_time(MYF(0)) > SYSVAR(failover_cooldown)) {
        master_election();
        fast_failover_count++;
        failure_counter = 0;
        failover_time_last = my_time(MYF(0));
      }
    }
  }
  DBUG_RETURN(0);
}

/*
  Initialize fast failover at server start or plugin installation.

  SYNOPSIS
    fast_failover_plugin_init()

  DESCRIPTION
    Starts up monitor thread

  RETURN VALUE
    0                    success
    1                    failure
*/

static int fast_failover_plugin_init(void *p)
{

  DBUG_ENTER("fast_failover_plugin_init");
  fast_failover_count = 0;
  fast_failover_master_status = 0;
  fast_failover_context *con;

  struct st_plugin_int *plugin= (struct st_plugin_int *)p;

  con= (struct fast_failover_context *)
    my_malloc(sizeof(struct fast_failover_context), MYF(0));

  if (!gtid_enabled) {
    fprintf(stderr,"Cannot failover without GTID AUTO_POSITION!\n");
    exit(0)
  }

  // check topology and capabilities tables
  // create if not exists
  if (!topo_db_exists()) {
    // should probably only do this from the master
    create_db_and_tables();
  }

  // read_only=0 indicates master instance
  if (local_service_is_writable()) {
     // check other services to see if they have
     // a higher gtid_executed. If so attempt to
     // set read-only and change master to the current
     // master. We don't know if we just started or our
     // our plugin was loaded, gtids ahead of us will
     // tells us if we died.
     if (replicas_ahead()) {
        set_read_only();
        catchup_from_replica();
        change_master_to();
     }
     else
     {
        // interrogate SHOW PROCESSLIST to find replicas
        if (replicas_connected())
    	  {
    	     // populate topology
           populate_tables();
    	  }
     }
  }
  // Need at least three voters for a quorum
  // if you need more voters a replica with
  // blackhole engines will suffice. Just ensure
  // that the read bit is unset on the service
  // and we will only use it for catchup,
  // and not election
  if (len(get_services() < 2)) {
    fprintf(stderr,"Not enough services for fast failover!\n");
    exit(0)
  }

  // start monitor thread
  pthread_attr_t attr;          /* Thread attributes */
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr,
                              PTHREAD_CREATE_JOINABLE);

  /* now create the thread */
  if (pthread_create(&con->monitor_thread, &attr, monitor_thread,
                     (void *)con) != 0)
  {
    fprintf(stderr,"Could not create fast failover monitor thread!\n");
    exit(0);
  }
  plugin->data= (void *)con;
  DBUG_RETURN(0);
}


/*
  Terminate the daemon example at server shutdown or plugin deinstallation.

  SYNOPSIS
    fast_failover_plugin_deinit()

  RETURN VALUE
    0                    success
    1                    failure (cannot happen)

*/

static int fast_failover_plugin_deinit(void *p)
{
  DBUG_ENTER("fast_failover_plugin_deinit");
  // detect if we're the master and promote?
  if (SYSVAR(on_shutdown)) {
    master_election();
  }

  struct st_plugin_int *plugin= (struct st_plugin_int *)p;
  struct fast_failover_context *con=
    (struct fast_failover_context *)plugin->data;
  void *dummy_retval;

  pthread_cancel(con->monitor_thread);


  /*
    Need to wait for the monitor thread to terminate before closing
    the file it writes to and freeing the memory it uses
  */
  pthread_join(con->monitor_thread, &dummy_retval);

  // Close connection to local_service mysql instance
  my_close(con->local_service, MYF(0));
  my_free(con);
  DBUG_RETURN(0);
}


struct st_mysql_daemon fast_failover_plugin=
{ MYSQL_DAEMON_INTERFACE_VERSION  };

/*
  Plugin library descriptor
*/

mysql_declare_plugin(fast_failover)
{
  MYSQL_DAEMON_PLUGIN,
  &fast_failover_plugin,
  "fast_failover",
  "Nathan Coffield",
  "Automatic master failure detection and promotion",
  PLUGIN_LICENSE_GPL,
  fast_failover_plugin_init, /* Plugin Init */
  fast_failover_plugin_deinit, /* Plugin Deinit */
  0x0100 /* 1.0 */,
  NULL,                       /* status variables                */
  NULL,                       /* system variables                */
  NULL,                       /* config options                  */
  0,                          /* flags                           */
}
mysql_declare_plugin_end;
