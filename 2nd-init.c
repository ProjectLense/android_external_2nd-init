/*
Copyright (C) 2010-2011 Skrilax_CZ (skrilax@gmail.com)
Using work done by Pradeep Padala (ptrace functions) (p_padala@yahoo.com)

Changes : Project Lense BMM (@whirleyes)
- add support for building 2nd-init as library (for multi-call binary)
- add propex function (hijack android property service)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/user.h>
#include <stdio.h>
#include <string.h>

#include "2nd-init.h"

union u 
{
	unsigned long val;
	char chars[sizeof(long)];
};

/* Get process data */
void get_data(pid_t child, unsigned long addr, char *str, int len)
{   
	char *laddr;
	int i, j;
	union u data;

	i = 0;
	j = len / sizeof(unsigned long);
	
	laddr = str;
	while(i < j) 
	{
		data.val = ptrace(PTRACE_PEEKDATA, child, (void*)(addr + i * 4), NULL);
		memcpy(laddr, data.chars, sizeof(unsigned long));
		++i;
		laddr += sizeof(unsigned long);
	}

	j = len % sizeof(unsigned long);
	
	if(j != 0) 
	{
		data.val = ptrace(PTRACE_PEEKDATA, child, (void*)(addr + i * 4), NULL);
		memcpy(laddr, data.chars, j);
	}
	
	str[len] = '\0';       
}

/* Put process data */
void put_data(pid_t child, unsigned long addr, char *str, int len)
{
	char *laddr;
	int i, j;
	
	union u data;

	i = 0;
	j = len / sizeof(unsigned long);
	laddr = str;
	while(i < j) 
	{
		memcpy(data.chars, laddr, sizeof(unsigned long));
		ptrace(PTRACE_POKEDATA, child, (void*)(addr + i * 4), (void*)(data.val));
		++i;
		laddr += sizeof(unsigned long);
	}

	j = len % sizeof(unsigned long);
	if(j != 0) 
	{
		memcpy(data.chars, laddr, j);
		ptrace(PTRACE_POKEDATA, child, (void*)(addr + i * 4), (void*)(data.val));
	}
}

/* Gets first free address */
unsigned long get_free_address(pid_t pid)
{
  FILE *fp;
  char filename[30];
  char line[85];
  char str[20];
  unsigned long addr;
  unsigned long end_address;
  sprintf(filename, "/proc/%d/maps", pid);
  fp = fopen(filename, "r");
  
  if(fp == NULL)
		exit(1);
		
  while(fgets(line, 85, fp) != NULL) 
  {
    sscanf(line, "%lx-%lx %s %s %s", &addr, 
    	&end_address, str, str, str);
    	
		if(strcmp(str, "00:00") == 0)
			break;
  }
  
  fclose(fp);
  return addr;
}

/* Gets image base data */
void get_base_image_data(pid_t pid, unsigned long* address, unsigned long* size, const char* name)
{
	FILE *fp;
  char filename[30];
  char line[85];
  char str[85];
  
  *address = 0;
	*size = 0;
  
  unsigned long start_address = 0;
  unsigned long end_address = 0;
  
  sprintf(filename, "/proc/%d/maps", pid);
  fp = fopen(filename, "r");
  
  if(fp == NULL)
		exit(1);
		
  while(fgets(line, 85, fp) != (char *)NULL) {
	if ((sscanf(line, "%lx-%lx %84[^/]/%s", &start_address, &end_address, str, str) == 4) && (!strcmp(str, name))) {
		*address = start_address;
		*size = end_address - start_address;
		break;
	}
  }
  
  fclose(fp);
}

#ifdef LIBRARY_VERSION
#define _GNU_SOURCE
#include <getopt.h>
#include <sched.h>
#include <cutils/properties.h>
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

extern struct prop_area *__system_property_area__;

/* Set ro.* property - based on goroh_kun's propex idea*/
int propex_main(int argc, char** argv) {
	argc--;
	argv++;

	int namelen = strlen(argv[1]);
    int valuelen = strlen(argv[2]);

    if(namelen >= PROP_NAME_MAX) return -1;
    if(valuelen >= PROP_VALUE_MAX) return -1;
    if(namelen < 1) return -1;

	char* prop_image;
	unsigned long image_base;
	unsigned long image_size;

	ptrace(PTRACE_ATTACH, 1, NULL, NULL);
	wait(NULL);

	get_base_image_data(1, &image_base, &image_size, "dev/__properties__");
	prop_image = malloc(image_size);
	get_data(1, image_base, prop_image, image_size);

	__system_property_area__ = (prop_area *) prop_image;
    prop_info *pi = (prop_info*) __system_property_find(argv[1]);

    if (pi != 0) {
		pi->serial = pi->serial | 1;
		memcpy(pi->value, argv[2], valuelen + 1);
		pi->serial = (valuelen << 24) | ((pi->serial + 1) & 0xffffff);
		put_data(1, image_base + (char*) pi - prop_image, (char*) pi, (int) sizeof(*pi));
    }

	free(prop_image);
	ptrace(PTRACE_DETACH, 1, NULL, NULL);
	return 0;
}
/* Set CPU affinity */
int setCPU(int pid, int totalcpu) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	int i;
	for(i=0; i<totalcpu; i++) {
		CPU_SET(i, &mask);
	}
	return sched_setaffinity(pid, sizeof(mask), &mask);
}
/* Get CPU affinity */
int getCPU(int pid) {
	cpu_set_t mask;
	sched_getaffinity(pid, sizeof(mask), &mask);
	pid = 0;
	int i;
	for(i=0; i<CPU_SETSIZE; i++) {
		if(CPU_ISSET(i,&mask)) {
			pid++;
		}
	}
	return pid;
}
/* library call*/
int second_init(int argc, char** argv) {
	klog_init();
	klog_set_level(6);
	
	int b4 = getCPU(1);
	INFO("Get CPU affinity: init %d, 2nd-init %d\n", b4, getCPU(0));
	INFO("Set CPU affinity...\n");
	setCPU(0,1);
	setCPU(1,1);
	INFO("Get CPU affinity: init %d, 2nd-init %d\n", getCPU(1), getCPU(0));
#else
/* Main */
int main(int argc, char** argv) {
#endif
	struct pt_regs regs;
	
	char buff[512];
	char init_env[512];
	char injected_data[1024];
	char* init_image;
	char* iter;
	
	unsigned long image_base;
	unsigned long image_size;
	unsigned long execve_address;
	unsigned long injected_data_address;
	unsigned long c,d;
		
	unsigned long execve_cur_env_ptr; 
	unsigned long execve_cur_env; 
	
	int found, len;
	
	/* Read the enviromental variables of the init */
	FILE* f = fopen("/proc/1/environ", "r");
	
	if (f == 0)
	{
		ERROR("Couldn't read /init enviromental variables.\n");
		return 2;
	}
	
	size_t sz = fread(init_env, 1, 511, f);
	init_env[sz] = 0;
	fclose(f);
	
	/* Init has always pid 1. If ptrace attach fails,
	 * insert a module to dismantle the check for init.
	 */
	memset(&regs, 0, sizeof(regs));
	ptrace(PTRACE_ATTACH, 1, NULL, NULL);
	
	/* wait for interrupt */
	wait(NULL);
		
	/* Obtain an address */
	injected_data_address = get_free_address(1);
	INFO("Address for data injection: 0x%08lX.\n", injected_data_address);
	
	/* Reset */
	memset(injected_data, 0, sizeof(injected_data));
	
	/* We want to call:
	 * execve("/init", { "/init", NULL }, envp);
	 */
	
  /* Get image data */
 	get_base_image_data(1, &image_base, &image_size, "init");
	
	if (image_base == 0 || image_size == 0)
	{
		ERROR("Error, couldn't get the image base of /init.\n");
		return 1;
	}
	
	INFO("image_base: 0x%08lX.\n", image_base);
	INFO("image_size: 0x%08lX.\n", image_size);
	
	init_image = malloc(image_size);
	get_data(1, image_base, init_image, image_size);
	
	/* Search for execve */
	execve_address = 0;
	c = 0;
												
	while (c < image_size - sizeof(execve_code))
	{
		found = 1;
		
		for(d = 0; d < sizeof(execve_code); d++)
		{
			if (init_image[c+d] != execve_code[d])
			{
				found = 0;
				break;
			}
		}
		
		if (found)
		{
			execve_address = image_base + c; 
			break;
		}
		
		c += 4; //ARM mode
	}
	
	if (!execve_address)
	{
		ERROR("Failed locating execve.\n");
		return 5;
	}
	
	INFO("execve located on: 0x%08lX.\n", execve_address);
	
	/* Fill in data:
	 *
	 * Offset - Description
	 *
	 * 0x0000 - char* - argument filename and argp[0] - pointer to "/init" on 0x0100
	 * 0x0004 - null pointer - argp[1] (set by memset)
	 * 0x0008 - envp[0] - first enviromental variable (starting on 0x0110)
	 * 0x000C - envp[1] - second enviromental variable
	 * 0x0010 - envp[2] - third enviromental variable
	 * etc. (null pointer behind the last set by memset)
	 * 0x0100 - "/init"
	 */
	
	/* Write execve arguments */
	
	/* "/init" goes to 0x100 */
	strcpy(&(injected_data[0x100]), "/init");
	
	/* Enviromental variables */
	execve_cur_env_ptr = injected_data_address + 0x10;
	execve_cur_env = injected_data_address + 0x110;
	iter = init_env;
		
	while (*iter)
	{
	  /* Write envp */		
		memcpy(&(injected_data[execve_cur_env_ptr - injected_data_address]), &execve_cur_env, sizeof(unsigned long));
		execve_cur_env_ptr += sizeof(unsigned long);
		
		/* Write string */
		strcpy(&(injected_data[execve_cur_env - injected_data_address]), iter);
		len = strlen(iter) + 1;
		
		iter += len;
		execve_cur_env += len;
		
		if (execve_cur_env % sizeof(unsigned long) != 0)
		  execve_cur_env += sizeof(unsigned long) - (execve_cur_env % sizeof(unsigned long));	
	}
	
	/* Put data to the process */
	put_data(1, injected_data_address, injected_data, sizeof(injected_data));
	
	/* Set registers 
	 *
	 * R0 = "/init"
	 * R1 = #argp
	 * R2 = #envp
   * PC = #execve 
	 */
	
	ptrace(PTRACE_GETREGS, 1, NULL, &regs);
	
	regs.ARM_r0 = injected_data_address + 0x0100; /* char*  filename */
	regs.ARM_r1 = injected_data_address + 0x0000; /* char** argp */
	regs.ARM_r2 = injected_data_address + 0x0008; /* char** envp */
	regs.ARM_pc = execve_address;
	
	NOTICE("Setting /init PC to: 0x%08lX.\n", execve_address);
	
	ptrace(PTRACE_SETREGS, 1, NULL, &regs);

  /* Detach */
	INFO("Detaching...\n");
	ptrace(PTRACE_DETACH, 1, NULL, NULL);
#ifdef LIBRARY_VERSION
	INFO("Unset CPU affinity...\n");
	setCPU(1,b4);
	INFO("Get CPU affinity: init %d\n", getCPU(1));
#endif
	return 0;
}


