/*

   lvm.c - Part of autopartkit, a module to partition devices
                  for debian-installer.

   Author - Petter Reinholdtsen

   Copyright (C) 2003  Petter Reinholdtsen <pere@hungry.com>
   
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

#define _GNU_SOURCE /* for asprintf() in <stdio.h> */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "autopartkit.h"

typedef enum {
    FALSE = 0,
    TRUE = 1
} BOOLEAN;

static const char *VGSCAN = "/sbin/vgscan";

static BOOLEAN
lvm_isinstalled(void)
{
    struct stat statbuf;
    static int isinstalled = -1;

    if (isinstalled != -1)
      return isinstalled;

    if (0 != stat("/etc/uselvm", &statbuf) || ! S_ISREG(statbuf.st_mode))
    {
        autopartkit_log(1, "Missing /etc/uselvm, no LVM support enabled.");
        isinstalled = FALSE;
        return FALSE;
    }

    /* Is /proc/lvm a directory? */
    if ( 0 != stat("/proc/lvm", &statbuf) || ! S_ISDIR(statbuf.st_mode) )
    {
        autopartkit_error(0, "Missing /proc/lvm/, no LVM support available.");
        isinstalled = FALSE;
        return FALSE;
    }
  
    /* Is /bin/vgscan available? */
    if (0 != stat(VGSCAN, &statbuf) || ! S_ISREG(statbuf.st_mode))
    {
        autopartkit_error(0, "Missing %s, no LVM support available.", VGSCAN);
        isinstalled = FALSE;
        return FALSE;
    }

    isinstalled = TRUE;
    return TRUE;
}

/*
 * Return true = 1 if the volume group exist, and false = 0 if it doesn't.
 */
static int
vg_exists(const char *vgname)
{
    struct stat statbuf;
    char *devpath = NULL;
    int retval = FALSE;

    if (NULL == vgname)
        return FALSE;

    if ( -1 == asprintf(&devpath, "/proc/lvm/VGs/%s", vgname) )
        autopartkit_error(0, "Unable to allocate string for vg '%s'", vgname);
    else
    {
        if (0 == stat(devpath, &statbuf) && S_ISDIR(statbuf.st_mode) )
	    retval = TRUE;
        free(devpath);
    }
    return retval;
}

int
lvm_init(void)
{
    int retval = -1;
    if ( ! lvm_isinstalled())
        return retval;

    if (0 == (retval = system("vgscan >> /var/log/messages 2>&1")))
	retval = 0;
    else
        autopartkit_log(2, "Executing vgscan returned error code %d\n",retval);

    return retval;
}

int
lvm_init_dev(const char *devpath)
{
    char * cmd = NULL;
    int retval = -1;

    if ( ! lvm_isinstalled())
        return -1;

    autopartkit_log(1, "Initializing LVM pv '%s'\n",
                    devpath ? devpath : "(null)");

    asprintf(&cmd, "pvcreate %s >> /var/log/messages 2>&1", devpath);
    if (cmd)
    {
        retval = system(cmd);
        free(cmd);
    }
    return retval;
}

/*
 * Create new volumegroup if it do not exist, and add new physical
 * device (devpath) to the voluemgroup if it exists.
 */
int
lvm_volumegroup_add_dev(const char *vgname, const char *devpath)
{
    int retval;
    char *progname = NULL;
    char * cmd = NULL;

    if ( ! lvm_isinstalled())
        return -1;

    autopartkit_log(1, "Adding LVM pv '%s' to vg '%s'\n",
                    devpath ? devpath : "(null)",
                    vgname ? vgname : "(null)");

    /* Call vgscan first, it seem to be required for vgcreate to work. */
    if (0 != (retval = system("vgscan >> /var/log/messages 2>&1")))
        autopartkit_log(2, "Executing vgscan returned error code %d\n",retval);

    if (vg_exists(vgname))
        progname = "vgextend";
    else
        progname = "vgcreate";

    asprintf(&cmd, "%s %s %s >> /var/log/messages 2>&1", progname,
             vgname, devpath);

    retval = -1;
    if (cmd)
    {
	retval = system(cmd);
        free(cmd);
    }
    return retval;
}

/*
 * Create Logical Volume in given volume group, and return device path
 * if successfull.  Return NULL on failure.  The string is malloc()ed,
 * and must be free()d by the caller.
 */
char *
lvm_create_logicalvolume(const char *vgname, const char *lvname,
                       unsigned int mbsize)
{
    char *cmd = NULL;
    char *devpath = NULL;
    int retval = -1;

    if ( ! lvm_isinstalled())
        return NULL;

    autopartkit_log(1, "Creating LVM lv '%s' on vg '%s' (size=%d MiB)\n",
                    lvname ? lvname : "(null)",
                    vgname ? vgname : "(null)",
                    mbsize);

    asprintf(&cmd, "lvcreate -n%s -L%d %s >> /var/log/messages 2>&1",
             lvname, mbsize, vgname);
    if (cmd)
    {
	retval = system(cmd);
	free(cmd);
	cmd=NULL;
    }

    if (0 == retval)
    {
        asprintf(&devpath, "/dev/%s/%s", vgname, lvname);
        return devpath;
    }
    else 
        return NULL;
}

/* Extract vgname, lvname and fstype from "lvm:tjener_vg:home0_lv:default". */
int
lvm_split_fstype(const char *str, int separator, int elemcount,
		 char *elements[])
{
    int elemnum;
    const char *curp = str;
    const char *nextp;

    for (elemnum = 0 ; elemnum < elemcount; elemnum++)
    {
	nextp = strchr(curp, separator);
	if (NULL == nextp)  /* Last element */
	  {
	    elements[elemnum] = strdup(curp);
	    autopartkit_log(3, "Found last '%s'\n", elements[elemnum]);
	    return 0;
	  }
	elements[elemnum] = strndup(curp, nextp - curp);
	autopartkit_log(3, "Found '%s'\n", elements[elemnum]);
	curp = nextp + 1;
    }
    
    return 0;
}

char *
lvm_lv_add(void *stack, const char *fstype, unsigned int mbsize)
{
    /* Create LVM logical volume with given FS.  Assuming LVM volume
       group is already created. */
    char *info[4]; /* 0='lvm', 1=vgname, 2=lvname, 3=fstype */

    /* Extract vgname, lvname and fstype from
       "lvm:tjener_vg:home0_lv:default". */
    if (0 != lvm_split_fstype(fstype, ':', 4, info))
    {
        autopartkit_log(0, "  Failed to parse '%s'\n", fstype);
	return NULL;
    }
    if (0 == strcmp("default", info[3]))
    {
        char *s = strdup(DEFAULT_FS);
	if (!s)
	    return NULL;
	else
	{
	    free(info[3]);
	    info[3] = s;
	}
    }
    autopartkit_log(2, "  Stacking LVM lv %s on vg %s "
		    "fstype %s\n", info[1], info[2], info[3]);
    /* Store vgname, lvname and size in stack */
    if (0 == lvm_lv_stack_push(stack, info[1], info[2], info[3], mbsize))
    {
        char buf[1024];
	snprintf(buf, sizeof(buf), "%s:%s", info[1], info[2]);
	return strdup(buf);
	
    }
    return NULL;
}

/* LVM stack operations */
struct lvm_pv_info {
    struct lvm_pv_info *next;
    char *vgname;
    char *devpath;
};

void *
lvm_pv_stack_new()
{
    struct lvm_pv_info *head;
    head = malloc(sizeof(*head));
    if (NULL != head)
    {
        head->next = head;
	head->vgname = NULL;
	head->devpath = NULL;
    }
    return head;
}
int
lvm_pv_stack_isempty(void *stack)
{
    struct lvm_pv_info *head = stack;
    return head->next == head;
}
int
lvm_pv_stack_push(void *stack, const char *vgname, const char *devpath)
{
    struct lvm_pv_info *head = stack;
    struct lvm_pv_info *elem;

    elem = malloc(sizeof(*elem));
    if (NULL == elem)
        return -1;
    elem->vgname = strdup(vgname);
    elem->devpath = strdup(devpath);

    elem->next = head->next;
    head->next = elem;

    return 0;
}
int
lvm_pv_stack_pop(void *stack, char **vgname, char **devpath)
{
    struct lvm_pv_info *head = stack;
    struct lvm_pv_info *elem;

    elem = head->next;

    if (elem == head)
        return -1;

    head->next = elem->next;

    *vgname = elem->vgname;
    *devpath = elem->devpath;

    free(elem);
  
    return 0;
}
int
lvm_pv_stack_delete(void *stack)
{
    char *vgname;
    char *devpath;
    while ( ! lvm_pv_stack_isempty(stack) )
    {
	lvm_pv_stack_pop(stack, &vgname, &devpath);
	free(vgname);
	free(devpath);
    }
    free(stack);
    return 0;
}

struct lvm_lv_info { /* Store vgname, lvname and mbsize in list */
    struct lvm_lv_info *next;
    char *vgname;
    char *lvname;
    char *fstype;
    unsigned int mbsize;
};

void *
lvm_lv_stack_new()
{
    struct lvm_lv_info *head;
    head = malloc(sizeof(*head));
    if (NULL != head)
    {
	head->next = head;
	head->vgname = NULL;
	head->lvname = NULL;
	head->mbsize = 0;
    }
    return head;
}
int
lvm_lv_stack_isempty(void *stack)
{
    struct lvm_lv_info *head = stack;
    return head->next == head;
}
int
lvm_lv_stack_push(void *stack, const char *vgname, const char *lvname,
		  const char *fstype, unsigned int mbsize)
{
    struct lvm_lv_info *head = stack;
    struct lvm_lv_info *elem;

    elem = malloc(sizeof(*elem));
    if (NULL == elem)
        return -1;
    elem->vgname = strdup(vgname);
    elem->lvname = strdup(lvname);
    elem->fstype = strdup(fstype);
    elem->mbsize = mbsize;

    elem->next = head->next;
    head->next = elem;

    return 0;
}
int
lvm_lv_stack_pop(void *stack, char **vgname, char **lvname, char **fstype,
		 unsigned int *mbsize)
{
    struct lvm_lv_info *head = stack;
    struct lvm_lv_info *elem;

    elem = head->next;

    if (elem == head)
        return -1;

    head->next = elem->next;

    *vgname = elem->vgname;
    *lvname = elem->lvname;
    *fstype = elem->fstype;
    *mbsize = elem->mbsize;

    free(elem);
  
    return 0;
}
int
lvm_lv_stack_delete(void *stack)
{
    char *vgname;
    char *lvname;
    while ( ! lvm_lv_stack_isempty(stack) )
    {
	lvm_pv_stack_pop(stack, &vgname, &lvname);
	free(vgname);
	free(lvname);
    }
    free(stack);
    return 0;
}
