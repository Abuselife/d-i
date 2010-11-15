/* 
   netcfg-static.c - Configure a static network for the debian-installer

   Copyright (C) 2000-2002  David Kimdon <dwhedon@debian.org>
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
   
*/
#include <ctype.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cdebconf/debconfclient.h>
#include <debian-installer.h>
#include <iwlib.h>
#include "netcfg.h"

int main(void)
{
    int num_interfaces = 0;
    static struct debconfclient *client;

    enum { BACKUP, GET_INTERFACE, GET_STATIC, WCONFIG, QUIT} state = GET_INTERFACE;


    /* initialize libd-i */
    di_system_init("netcfg-static");

    /* initialize debconf */
    client = debconfclient_new();
    debconf_capb(client, "backup");

    while (1) {
	switch(state) {
	case BACKUP:
	    return 10;
	case GET_INTERFACE:
	    if (netcfg_get_interface(client, &interface, &num_interfaces))
		state = BACKUP;
	    else
	    {
	      if (is_wireless_iface(interface))
		state = WCONFIG;
	      else
	        state = GET_STATIC;
	    }
	    break;
	case GET_STATIC:
	    if (netcfg_get_static(client)) 
		state = (num_interfaces == 1) ? BACKUP : GET_INTERFACE;
	    else
		state = QUIT;
	    break;
	case WCONFIG:
	    if (netcfg_wireless_set_essid (client, interface)
		|| netcfg_wireless_set_wep (client, interface))
	    {
	      state = BACKUP;
	      break;
	    }
	    state = GET_STATIC;
	    break;
	case QUIT:
	    if (netcfg_activate_static(client) != 0) 
		return 1;
	    return 0;
	}
    }

    return 0;
}