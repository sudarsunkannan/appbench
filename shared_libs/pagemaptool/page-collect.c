/* page-collect.c -- collect a snapshot each of of the /proc/pid/maps files, 
 *      with each VM region interleaved with a list of physical addresses 
 *      which make up the virtual region.
 * Copyright C2009 by EQware Engineering, Inc.
 *
 *    page-collect.c is part of PageMapTools.
 *
 *    PageMapTools is free software: you can redistribute it and/or modify
 *    it under the terms of version 3 of the GNU General Public License
 *    as published by the Free Software Foundation
 *
 *    PageMapTools is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with PageMapTools.  If not, see http://www.gnu.org/licenses. 
 */
#define _LARGEFILE64_SOURCE

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define FILENAMELEN         256
#define LINELEN             256
#define PAGE_SIZE           4096

#define PROC_DIR_NAME       "/proc"
#define MAPS_NAME           "maps"
#define PAGEMAP_NAME        "pagemap"
#define CMDLINE_NAME        "cmdline"
#define STAT_NAME           "stat"
#define OUT_NAME            "./page-collect.dat"

typedef int bool;
#define FALSE               0
#define TRUE                (!FALSE)


/* ERR() --
 */
#define ERR(format, ...) fprintf(stderr, format, ## __VA_ARGS__)


/* is_switch() --
 */
static inline bool is_switch(char c)
    { return c == '-';
    }


/* is_directory() --
 */
static bool is_directory(const char *dirname)
{
    struct stat buf;
    int n;

    assert(dirname != NULL);

    n = stat(dirname, &buf);

    return (n == 0 && (buf.st_mode & S_IFDIR) != 0)? TRUE: FALSE;

}   /* is_directory */


/* is_wholly_numeric() --
 */
static bool is_wholly_numeric(const char *str)
{
    assert(str != NULL);

    while (*str != '\0')
    {
        if (!isdigit(*str))
        {
            return FALSE;
        }
        str++;
    }
    return TRUE;

}   /* is_wholly_numeric */


/* main() --
 */
int main(int argc, char *argv[])
{
    struct dirent *de;
    int n;

    int pm    = -1;
    FILE *m   = NULL;
    FILE *out = NULL;
    DIR *proc = NULL;
    int retval = 0;
    char pid[256];	

    const char *out_name = OUT_NAME;

    /* Process command-line arguments.
     */
    for (n = 1; n < argc; n++)
    {
        char *arg = argv[n];

        if (is_switch(*arg))
        {
            arg++;

            /* -o out-file 
             */
            if (strncmp(arg, "o", 1) == 0)
            {
                if (arg[1] == '\0') arg = argv[++n];
                else                arg++;

                out_name = arg;
	        printf("Collecting details for process %s\n",arg);
            }
            else if (strncmp(arg, "P", 1) == 0)
            {
                if (arg[1] == '\0') arg = argv[++n];
                else                arg++;

                strcpy(pid,arg);
		printf("Collecting details for process %s\n",arg);
            }
            /* Unknown switch.
             */
            else {
		printf("Collecting details for process %s\n",arg);
		goto usage;
    	    }
        }
        else
        {
          usage:
            fprintf(stderr, 
                "\n"
                "page-collect -- collect a snapshot each of of the /proc/pid/maps\n"
                "  files, with each VM region interleaved with a list of physical\n"
                "  addresses which make up the virtual region.\n"
                "\n"
                "usage: page-collect {switches}\n"
                "switches:\n"
                " -o out-file          -- Output file name (def=%s)\n"
                "\n",
                OUT_NAME);
            goto done;
        }
    }

    /* Open output file for writing.
     */
    out = fopen(out_name, "w");
    if (out == NULL)
    {
        ERR("Unable to open file \"%s\" for writing (errno=%d). (1)\n", out_name, errno);
        retval = -1;
        goto done;
    }

    /* Open /proc directory for traversal.
     */
    proc = opendir(PROC_DIR_NAME);
    if (proc == NULL)
    {
        ERR("Unable to open directory \"%s\" for traversal (errno=%d). (4)\n", PROC_DIR_NAME, errno);
        retval = -1;
        goto done;
    }

    /* For each entry in the /proc directory...
     */
    goto enter;
    while (de != NULL)
    {
        char d_name[FILENAMELEN];

	if(strlen(pid)) {
	  if(strcmp(de->d_name,pid)){
		goto enter;	
	  }
	  else {
		printf("collecting details for the process %s\n",de->d_name); 	
	  }		
	}
	sprintf(d_name, "%s/%s", PROC_DIR_NAME, de->d_name);

        /* ...if the entry is a numerically-named directory...
         */
        if (is_directory(d_name)  &&  is_wholly_numeric(de->d_name))
        {
            char m_name[FILENAMELEN];
            char pm_name[FILENAMELEN];
            char cl_name[FILENAMELEN];
            char p_name[LINELEN];
            char line[LINELEN];

            FILE *cl = NULL;

            /* Open pid/maps file for reading.
             */
            sprintf(m_name, "%s/%s/%s", PROC_DIR_NAME, de->d_name, MAPS_NAME);
            m = fopen(m_name, "r");
            if (m == NULL)
            {
                ERR("Unable to open \"%s\" for reading (errno=%d) (5).\n", m_name, errno);
                continue;
            }

            /* Open pid/pagemap file for reading.
             */
            sprintf(pm_name, "%s/%s/%s", PROC_DIR_NAME, de->d_name, PAGEMAP_NAME);
            pm = open(pm_name, O_RDONLY);
            if (pm == -1)
            {
                ERR("Unable to open \"%s\" for reading (errno=%d). (7)\n", pm_name, errno);
                continue;
            }

            /* Get process command-line or name string.
             */
            p_name[0] = '\0';

            /* Open pid/cmdline file for reading.  Try for command-line.
             */
            sprintf(cl_name, "%s/%s/%s", PROC_DIR_NAME, de->d_name, CMDLINE_NAME);
            cl = fopen(cl_name, "r");
            if (cl == NULL)
            {
                ERR("Unable to open \"%s\" for reading (errno=%d). (7.1)\n", cl_name, errno);
            }
            fgets(p_name, LINELEN, cl);
            fclose(cl);

            /* If no command-line was available, get the second field of the first
             *  line of the pid/stat file.
             */
            if (strlen(p_name) == 0)
            {
                FILE *st;
                char st_name[FILENAMELEN];
                char stat[LINELEN];
                char *s;
                int i;

                /* Open pid/stat file for reading.
                 */
                sprintf(st_name, "%s/%s/%s", PROC_DIR_NAME, de->d_name, STAT_NAME);
                st = fopen(st_name, "r");
                if (st == NULL)
                {
                    ERR("Unable to open \"%s\" for reading (errno=%d). (7.2)\n", st_name, errno);
                }
                fgets(stat, LINELEN, st);
                fclose(st);

                s = stat;
                i = 0;
                while (*s != '\0' && !isspace(*s)) s++;
                while (*s != '\0' &&  isspace(*s)) s++;
                while (*s != '\0' && !isspace(*s)) p_name[i++] = *s++;
                p_name[i] = '\0';
            }

            /* For each maps file, output the filename and process info (ps entry).
             */
            fprintf(out, "@ %s - %s\n", m_name, p_name);

            /* For each line in the maps file...
             */
            while (fgets(line, LINELEN, m) != NULL)
            {
                unsigned long vm_start;
                unsigned long vm_end;
                int num_pages;

                /* ...output the line...
                 */
                fprintf(out, "= %s", line);

                /* ...then evaluate the range of virtual
                 *  addresses it asserts.
                 */
                n = sscanf(line, "%lX-%lX", &vm_start, &vm_end);
                if (n != 2)
                {
                    ERR("Invalid line read from \"%s\": %s (6)\n", m_name, line);
                    continue;
                }

                /* If the virtual address range is greater than 0...
                 */
                num_pages = (vm_end - vm_start) / PAGE_SIZE;
                if (num_pages > 0)
                {
                    long index = (vm_start / PAGE_SIZE) * sizeof(unsigned long long);
                    off64_t o;
                    ssize_t t;

                    /* Seek to appropriate index of pagemap file.
                     */
                    o = lseek64(pm, index, SEEK_SET);
                    if (o != index)
                    {
                        ERR("Error seeking to %ld in file \"%s\" (errno=%d). (8)\n", index, pm_name, errno);
                        continue;
                    }

                    /* For each page in the virtual address range...
                     */
                    while (num_pages > 0)
                    {
                        unsigned long long pa;

                        /* Read a 64-bit word from each of the pagemap file...
                         */
                        t = read(pm, &pa, sizeof(unsigned long long));
                        if (t < 0)
                        {
                            ERR("Error reading file \"%s\" (errno=%d). (11)\n", pm_name, errno);
                            goto do_continue;
                        }

                        /* ...and write the word to a single line of output.
                         */
                        fprintf(out, ": %016llX\n", pa);

                        num_pages--;
                    }
                }
              do_continue:
                ;
            }
            if (pm != -1)
            {
                close(pm); 
                pm = -1;
            }
        }
        if (m != NULL)
        {
            fclose(m); 
            m = NULL;
        }

      enter:
        de = readdir(proc);
    }

  done:
    if (proc != NULL)
    {
        closedir(proc);
    }
    if (pm != -1)
    {
        close(pm);
    }
    if (m != NULL)
    {
        fclose(m);
    }
    if (out != NULL)
    {
        fclose(out);
    }
    return retval;

}  /* main */

