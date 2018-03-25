// page-analyze.cpp -- analyze a snapshot file created by page-collect
//    and generate specified reports.
// Copyright, C2009 by EQware Engineering, Inc.
//
//    page-analyze.cpp is part of PageMapTools.
//
//    PageMapTools is free software: you can redistribute it and/or modify
//    it under the terms of version 3 of the GNU General Public License
//    as published by the Free Software Foundation
//
//    PageMapTools is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with PageMapTools.  If not, see http://www.gnu.org/licenses. 
//

#define _LARGEFILE64_SOURCE

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>
#include <algorithm>

#define FILENAMELEN             256
#define LINELEN                 256
#define PAGE_SIZE               4096
#define MB                      0x100000

#define IN_NAME                 "./page-collect.dat"
#define OUT_NAME                "./page-analyze.dat"

#define FLAGS_IN_MB             0x00000001
#define FLAGS_COMPONENT_RPT     0x00000010
#define FLAGS_PROCESS_RPT       0x00000020
#define FLAGS_PROCVSCOMP_RPT    0x00000040
#define FLAGS_CSV_FMT           0x00010000


//class mem_stats_t --
//
class mem_stats_t
{
  public:
    unsigned long long uss;
    double             pss;
    unsigned long long rss;
    unsigned long long vss;

    mem_stats_t() { uss = rss = vss = 0; pss = 0.0; }

};  //class mem_stats_t


//class proc_mem_t --
//
class proc_mem_t
{
  public:
    std::string           name;
    mem_stats_t           stats;
    unsigned              comp_usage[200];

    proc_mem_t() 
        { name = ""; 
          memset(&stats, 0, sizeof(stats)); 
          memset(comp_usage, 0, sizeof(comp_usage));
        }
    proc_mem_t(std::string p, mem_stats_t &s) 
        { name = p; 
          stats = s; 
          memset(comp_usage, 0, sizeof(comp_usage));
        }
};  //class proc_mem_t


//ERR() --
//
#define ERR(format, ...) fprintf(stderr, format, ## __VA_ARGS__)


//is_switch() -- Returns true if the character c is a switch character, false otherwise.
//
static inline bool is_switch(char c)
    { return c == '-';
    }


//comp_pair_uss_gt() --
//
static bool comp_pair_uss_gt(const std::pair<std::string, const mem_stats_t *> &a, 
                             const std::pair<std::string, const mem_stats_t *> &b)
{ 
    return (a.second->uss > b.second->uss); 

}   //comp_pair_uss_gt(const std::pair<std::string, const mem_stats_t *> &, 
    //                 const std::pair<std::string, const mem_stats_t *> &)


//proc_mem_uss_gt() --
//
static bool proc_mem_uss_gt(const proc_mem_t *a, const proc_mem_t *b)
{ 
    return (a->stats.uss > b->stats.uss);

}   //proc_mem_uss_gt(const proc_mem_t *, const proc_mem_t *)


//make_short_name() --
//
static void make_short_name(char *buf, const char *long_name)
{
   char *b = (char *)strrchr(long_name, '/');
    if (b == NULL) 
    {
        b = (char *)long_name;
        if (*b == '\0')
        {
            b = "ANON";
        }
        else if (*b == '[')
        {
            b++;
            if (strchr(b, ']') != NULL)
            {
                *strchr(b, ']') = '\0';
            }
        }
    }
    else
    {
        b++;
    }

    strcpy(buf, b);
    buf[5] = '\0';

}   //make_short_name(char *, const char *)


//print_report_line() --
//
static void print_report_line(FILE *out, unsigned long flags,
                              unsigned long long uss, double pss,
                              unsigned long long rss, unsigned long long vss,
                              const char *info)
{
    if (flags & FLAGS_IN_MB)
    {
        fprintf(out, "%10.2lf %12.2lf %10.2lf %10.2lf : %s\n",
                double(uss * PAGE_SIZE) / MB, 
                double(pss * PAGE_SIZE) / MB, 
                double(rss * PAGE_SIZE) / MB, 
                double(vss * PAGE_SIZE) / MB, 
                info);
    }
    else
    {
        fprintf(out, "%10llu %12.2lf %10llu %10llu : %s\n",
                uss, 
                pss, 
                rss, 
                vss, 
                info);
    }
}   //print_report_line(FILE *, unsigned long, unsigned long long, double,
    //                  unsigned long long, unsigned long long, const char *)


//build_phys_page_usage_map() -- Build an associative array consisting of a count 
//  of references for each mapped physical page.
//
static void build_phys_page_usage_map(FILE *in, unsigned long flags, 
                                      std::map<unsigned long long, unsigned> &pa_map)
{
    char line[LINELEN];

    assert(in != NULL);

    //For each physical page mapping entry in the input file...
    //
    while (fgets(line, LINELEN, in) != NULL)
    {
        if (*line == ':')
        {
            //...increment the entry in the pa_map tree (create
            //  the entry and set to 1, initially).
            //
            unsigned long long pa = strtoull(line + 9, NULL, 16);
            if (pa_map.find(pa) == pa_map.end())
            {
                pa_map[pa] = 1;
            }
            else
            {
                pa_map[pa]++;
            }
        }
    }
}   ///build_phys_page_usage_map(FILE *, unsigned long,
    //                           std:map<unsigned long long, unsigned> &)


//generate_process_report() -- Output a per-process report, giving the memory usage
//  for each process.  Output consists of:
//      USS (Unique Set Size) -- the number of mapped physical pages which are
//           referenced ONLY by the associated process.
//      PSS (Proportional Set Size) -- the number of mapped physical pages which are 
//          referenced by the associated process, where shared pages are counted
//          toward each process in the proportion by which they are shared.
//      RSS (Resident Set Size) -- the number of mapped physical pages referenced by
//          the associated process.
//      VSS (Virtual Set Size) -- the number of pages, mapped or unmapped, which are
//          assigned to the associated process
//
static void generate_process_report(FILE *in, FILE *out, unsigned long flags,
                                    std::map<unsigned long long, unsigned> &pa_map,
                                    std::map<std::string, proc_mem_t> *proc_map)
{
    char line[LINELEN];
    char process[LINELEN];

    mem_stats_t ms;
    mem_stats_t msc;

    assert(in != NULL);
    assert(out != NULL);

    if ((flags & FLAGS_PROCESS_RPT) != 0)
    {
        fprintf(out, "\n");
        fprintf(out, "       USS          PSS        RSS    Virtual : Process\n");
    }

    //For each line in the input file...
    //
    process[0] = '\0';
    while (fgets(line, LINELEN, in) != NULL)
    {
        //If a new process entry is seen...
        //
        if (*line == '@')
        {
            unsigned j;

            //Write out the previous process, including all the counters and the process string.
            //
          last_line:
            if (ms.vss != 0)
            {
                if ((flags & FLAGS_PROCESS_RPT) != 0)
                {
                    print_report_line(out, flags, ms.uss, ms.pss, ms.rss, ms.vss, process);
                }
                msc.uss += ms.uss;
                msc.pss += ms.pss;
                msc.rss += ms.rss;
                msc.vss += ms.vss;

                if (proc_map != NULL)
                {
                    (*proc_map)[process].name = process + 2;
                    (*proc_map)[process].name.resize(26);
                    (*proc_map)[process].stats = ms;
                }
            }

            //Reset to read in the new process.
            //
            ms.uss = 0;
            ms.pss = 0.0;
            ms.rss = 0;
            ms.vss = 0;

            j = strtoul(line + 8, NULL, 10);
            sprintf(process, "%6u%s", j, strchr(line + 8, ' '));
            process[strlen(process) - 1] = '\0';
        }

        //If a maps file entry is seen...
        //
        else if (*line == '=')
        {
            ; //do nothing
        }

        //If a physical page address entry is seen...
        //
        else if (*line == ':')
        {
            //Get the numeric page address value.
            //
            unsigned long long pa = strtoull(line + 9, NULL, 16);

            //Increment the appropriate counters.
            //
            ms.vss++;
            if (pa != 0) 
            {
                ms.rss++;
                ms.pss += 1.0 / pa_map[pa];
                if (pa_map[pa] == 1) 
                {
                    ms.uss++;
                }
            }
        }

        //Should be impossible -- invalid input file.
        //
        else
        {
            assert(false);
        }
    }

    //Write out the previous process, including all the counters and the process string.
    //
    if (ms.uss != 0)
    {
        goto last_line;
    }

    //Write out a total line.
    //
    if ((flags & FLAGS_PROCESS_RPT) != 0)
    {
        print_report_line(out, flags, msc.uss, msc.pss, msc.rss, msc.vss,
                          (flags & FLAGS_IN_MB)? " TOTAL (in MBytes)":" TOTAL (in pages)");
    }
    fprintf(out, "\n");

}   //generate_process_report(FILE *, FILE *, unsigned long,
    //                        std::map<std::string, proc_mem_t> &,
    //                        std::map<unsigned long long, unsigned> &)


//build_component_page_map() --
//
static void build_component_page_map(FILE *in, unsigned long flags,
                                     std::map<unsigned long long, unsigned> &pa_map, 
                                     std::map<std::string, mem_stats_t> &comp_map)
{
    char line[LINELEN];

    mem_stats_t *ms = NULL;

    assert(in != NULL);

    //For each line in the input file...
    //
    while (fgets(line, LINELEN, in) != NULL)
    {
        line[strlen(line) - 1] = '\0';

        //If a new process entry is seen...
        //
        if (*line == '@')
        {
            ;   //do nothing
        }

        //If a maps file entry is seen...
        //
        else if (*line == '=')
        {
            //Get a pointer to the comp_map entry for this component.
            //  Create the entry if needed.
            //
            char *l = line + 40;                        //MAGIC NUMBER
            while (*l != '\n' && !isspace(*l)) l++;
            while (*l != '\n' &&  isspace(*l)) l++;
            ms = &comp_map[l];
        }

        //If a physical page address entry is seen...
        //
        else if (*line == ':')
        {
            assert(ms != NULL);

            //Get the numeric page address value.
            //
            unsigned long long pa = strtoull(line + 9, NULL, 16);

            //Increment the appropriate counters.
            //
            ms->vss++;
            if (pa != 0) 
            {
                ms->rss++;
                ms->pss += 1.0 / pa_map[pa];
                if (pa_map[pa] == 1) 
                {
                    ms->uss++;
                }
            }
        }

        //Should be impossible -- invalid input file.
        //
        else
        {
            assert(false);
        }
    }
}   //build_component_page_map(FILE *, unsigned long,
    //                         std::map<unsigned long long, unsigned> &,
    //                         std::map<std::string, mem_stats_t> &)


//generate_component_report() --
//
static void generate_component_report(FILE *out, unsigned long flags,
                                      std::map<std::string, mem_stats_t> &comp_map)
{
    std::map<std::string, mem_stats_t>::iterator ci;

    assert(out != NULL);

    fprintf(out, "\n");
    fprintf(out, "       USS          PSS        RSS    Virtual : Component\n");

    for (ci = comp_map.begin(); ci != comp_map.end(); ci++)
    {
        mem_stats_t *ms = &(*ci).second;

        print_report_line(out, flags, ms->uss, ms->pss, ms->rss, ms->vss, 
                                        ((*ci).first).c_str());
    }
    fprintf(out, "\n");

}   //generate_component_report(FILE *, unsigned long, 
    //                          std::map<std::string, mem_stats_t> &)


//build_comp_mag_xref() -- Builds two cross-reference tables:
//  1. mag_comp_xref is a vector of component names/stats indexed
//      by decreasing component USS.
//  2. comp_mag_xref is a map of mag_comp_xref indices, themselves indexed
//      by component name.
//
static void build_comp_mag_xref(
    const std::map<std::string, mem_stats_t> &comp_map, 
    std::vector< std::pair<std::string, const mem_stats_t *> > &mag_comp_xref,
    std::map<std::string, unsigned> &comp_mag_xref)
{
    //Copy all of the comp_map elements to a list.  
    //  Sort the list from largest USS to smallest.
    //
    for (std::map<std::string, mem_stats_t>::const_iterator it = comp_map.begin(); 
          it != comp_map.end(); 
          it++)
    {
        mag_comp_xref.push_back(make_pair(it->first, &it->second));
    }
    sort(mag_comp_xref.begin(), mag_comp_xref.end(), comp_pair_uss_gt);

    //Add each mag_comp_xref string and index to comp_mag_xref.
    //
    for (unsigned i = 0; i < mag_comp_xref.size(); i++)
    {
        comp_mag_xref[mag_comp_xref[i].first] = i;
    }
}   //build_comp_mag_xref(
    //  const std::map<std::string, mem_stats_t> &,
    //  std::vector< std::pair<std::string, const mem_stats_t *> > &,    
    //  std::map<std::string, unsigned> comp_mag_xref &)


//build_comp_usage_table() --
//
static void build_comp_usage_table(
    FILE *in, 
    unsigned flags, 
    std::map<std::string, proc_mem_t> &proc_map,
    const std::map<std::string, unsigned> &comp_mag_xref)
{
    char line[LINELEN];
    std::string proc = "";
    std::string comp = "";

    assert(in != NULL);

    //For each line in the input file...
    //
    while (fgets(line, LINELEN, in) != NULL)
    {
        line[strlen(line) - 1] = '\0';

        //If a new process entry is seen...
        //
        if (*line == '@')
        {
            char buf[LINELEN];
            unsigned j = strtoul(line + 8, NULL, 10);
            sprintf(buf, "%6u%s", j, strchr(line + 8, ' '));
            proc = buf;
        }

        //If a maps file entry is seen...
        //
        else if (*line == '=')
        {
            //Get a pointer to the comp_map entry for this component.
            //  Create the entry if needed.
            //
            char *l = line + 40;                        //MAGIC NUMBER
            while (*l != '\n' && !isspace(*l)) l++;
            while (*l != '\n' &&  isspace(*l)) l++;
            comp = l;
        }

        //If a physical page address entry is seen...
        //
        else if (*line == ':')
        {
            //Skip upper characters of page address.
            //s
            if (strtoull(line + 9, NULL, 16) != 0)
            {
                proc_map[proc].comp_usage[comp_mag_xref.find(comp)->second]++;
            }
        }

        //Should be impossible -- invalid input file.
        //
        else
        {
            assert(false);
        }
    }
}   //build_comp_usage_table(
    //  FILE *,                                         
    //  unsigned,                                   
    //  std::map<std::string, proc_mem_t> &,             
    //  const std::map<std::string, unsigned> &)


//generate_proc_vs_comp_report() --
//
static void generate_proc_vs_comp_report(
    FILE *out, 
    unsigned flags, 
    const std::vector<proc_mem_t *> &proc_list, 
    const std::vector< std::pair<std::string, const mem_stats_t *> > &mag_comp_xref)
{
    if ((flags & FLAGS_CSV_FMT) != 0)
    {
        //Header line #1: Process USS/PSS/RSS labels + Component names.
        //
        fprintf(out, "\"PID  - PROCESS NAME\",\"USS\",\"PSS\",\"RSS\"");  
        for (unsigned m = 0; m < mag_comp_xref.size(); m++)
        {
            if (*mag_comp_xref[m].first.c_str() == '\0')
                fprintf(out, ",\"%s\"", "[anon]");
            else
                fprintf(out, ",\"%s\"", mag_comp_xref[m].first.c_str());
        }
        fprintf(out, "\n");

        //Process lines: One line for each process, in form
        //  PID - Name : USS PSS RSS : comp-ref comp-ref ...
        //
        for (unsigned n = 0; n < proc_list.size(); n++)
        {
            const proc_mem_t *p = proc_list[n];

            fprintf(out, "\"%s\",%lld,%lf,%lld", 
                p->name.c_str(), p->stats.uss, p->stats.pss, p->stats.rss);

            for (unsigned m = 0; m < mag_comp_xref.size(); m++)
            {
                fprintf(out, ",%u", p->comp_usage[m]);
            }
            fprintf(out, "\n");
        }
        fprintf(out, "\n\n");
    }
    else
    {
        //Header line #0: Component indices.
        //
        fprintf(out, "                                                  :");  
        for (unsigned m = 0; m < mag_comp_xref.size(); m++)
        {
            fprintf(out, " %5u", m + 1);
        }
        fprintf(out, "\n");
    
        //Header line #1: Process USS/PSS/RSS labels + Component short-names.
        //
        fprintf(out, "PID  - PROCESS NAME        :   USS      PSS   RSS :");  
        for (unsigned m = 0; m < mag_comp_xref.size(); m++)
        {
            char buf[LINELEN];
            make_short_name(buf, mag_comp_xref[m].first.c_str());
            fprintf(out, " %5s", buf);
        }
        fprintf(out, "\n");
    
        //Separator line.
        //
        fprintf(out, "---- - ------------------- - ----- -------- ----- -");
        for (unsigned m = 0; m < mag_comp_xref.size(); m++)
        {
            fprintf(out, " -----");
        }
        fprintf(out, "\n");
    
        //Process lines: One line for each process, in form
        //  PID - Name : USS PSS RSS : comp-ref comp-ref ...
        //
        for (unsigned n = 0; n < proc_list.size(); n++)
        {
            const proc_mem_t *p = proc_list[n];
    
            fprintf(out, "%-26s : %5lld %8.2lf %5lld :", 
                p->name.c_str(), p->stats.uss, p->stats.pss, p->stats.rss);
    
            for (unsigned m = 0; m < mag_comp_xref.size(); m++)
            {
                fprintf(out, " %5u", p->comp_usage[m]);
            }
            fprintf(out, "\n");
    
            if (((n + 1) % 5) == 0)
            {
                //Separator line.
                //
                fprintf(out, "---- - ------------------- - ----- -------- ----- -");
                for (unsigned m = 0; m < mag_comp_xref.size(); m++)
                {
                    fprintf(out, " -----");
                }
                fprintf(out, "\n");
            }
        }
    
        //Separator line.
        //
        fprintf(out, "---- - ------------------- - ----- -------- ----- -");
        for (unsigned m = 0; m < mag_comp_xref.size(); m++)
        {
            fprintf(out, " -----");
        }
        fprintf(out, "\n");
    
        //Footer line #1: Component short-names.
        //
        fprintf(out, "                                                  :");  
        for (unsigned m = 0; m < mag_comp_xref.size(); m++)
        {
            char buf[LINELEN];
            make_short_name(buf, mag_comp_xref[m].first.c_str());
            fprintf(out, " %5s", buf);
        }
        fprintf(out, "\n\n");
    
        //Legend header line #1:
        //
        fprintf(out, "    SNAME : LONG NAME\n");
        for (unsigned m = 0; m < mag_comp_xref.size(); m++)
        {
            char buf[LINELEN];
            make_short_name(buf, mag_comp_xref[m].first.c_str());
            fprintf(out, "%3u %5s : %s\n", m + 1, buf, mag_comp_xref[m].first.c_str());
        }
        fprintf(out, "\n");
    }
}   //generate_proc_vs_comp_report(
    //  FILE *,
    //  unsigned,
    //  const std::vector<proc_mem_t *> &,
    //  const std::vector< std::pair<std::string, const mem_stats_t *> > &)


//main() --
//
int main(int argc, char *argv[])
{
    int n;
    FILE *in;
    FILE *out;
    std::map<unsigned long long, unsigned> pa_map;
    std::map<std::string, mem_stats_t> comp_map;
    std::map<std::string, unsigned> comp_mag_xref;
    std::vector< std::pair<std::string, const mem_stats_t *> > mag_comp_xref;
    std::map<std::string, proc_mem_t> proc_map;
    std::vector<proc_mem_t *> proc_list;

    const char *in_name = IN_NAME;
    const char *out_name = OUT_NAME;

    unsigned long flags = 0;
    int retval = 0;

    //Process command-line arguments.
    //
    for (n = 1; n < argc; n++)
    {
        char *arg = argv[n];

        if (is_switch(*arg))
        {
            arg++;

            if (strncmp(arg, "c", 1) == 0)
            {
                flags |= FLAGS_COMPONENT_RPT;
            }

            // -i in-file 
            //
            else if (strncmp(arg, "i", 1) == 0)
            {
                if (arg[1] == '\0') arg = argv[++n];
                else                arg++;

                in_name = arg;
            }

            // -mcsv
            //
            else if (strncmp(arg, "mcsv", 4) == 0)
            {
                flags |= (FLAGS_PROCVSCOMP_RPT|FLAGS_CSV_FMT);
            }

            // -m
            //
            else if (strncmp(arg, "m", 1) == 0)
            {
                flags |= FLAGS_PROCVSCOMP_RPT;
            }

            // -Mb 
            //
            else if (strncmp(arg, "Mb", 2) == 0)
            {
                flags |= FLAGS_IN_MB;
            }

            // -o out-file 
            //
            else if (strncmp(arg, "o", 1) == 0)
            {
                if (arg[1] == '\0') arg = argv[++n];
                else                arg++;

                out_name = arg;
            }

            else if (strncmp(arg, "p", 1) == 0)
            {
                flags |= FLAGS_PROCESS_RPT;
            }

            // Unknown switch.
            //
            else goto usage;
        }
        else
        {
          usage:
            fprintf(stderr, "\n"
                            "page-analyze -- analyze a snapshot file created by page-collect\n"
                            "  and generate specified reports.\n"
                            "\n"
                            "usage: page-analyze {switches}\n"
                            "switches:\n"
                            " -c           -- Generate component report.\n"
                            " -i in-file   -- Input file name (def=%s)\n"
                            " -m           -- Generate process/component matrix.\n"
                            " -mcsv        -- Generate matrix in CSV format.\n"
                            " -Mb          -- Report in Mbytes (def=pages)\n"
                            " -o out-file  -- Output file name (def=%s)\n"
                            " -p           -- Generate process report.\n"
                            "\n",
                            IN_NAME, 
                            OUT_NAME);
            goto done;
        }
    }

    // Open output file for writing.
    //
    out = fopen(out_name, "w");
    if (out == NULL)
    {
        ERR("Unable to open file \"%s\" for writing (errno=%d). (1)\n", out_name, errno);
        retval = -1;
        goto done;
    }

    // Open input file for reading.
    //
    in = fopen(in_name, "r");
    if (in == NULL)
    {
        ERR("Unable to open file \"%s\" for reading (errno=%d). (2)\n", in_name, errno);
        retval = -1;
        goto done;
    }

    //Build a table of physical page addresses -> reference-counts for that page.
    //
    build_phys_page_usage_map(in, flags, pa_map);

    if ((flags & (FLAGS_PROCESS_RPT|FLAGS_PROCVSCOMP_RPT)) != 0)
    {
        //Rewind input file.
        //
        n = fseek(in, 0, SEEK_SET);
        if (n != 0)
        {
            ERR("Unable to rewind file \"%s\" for reading (errno=%d). (3)\n", in_name, errno);
            retval = -1;
            goto done;
        }

        //Generate a report of memory usage for each process.
        //
        generate_process_report(in, out, flags, pa_map, &proc_map);
    }

    if ((flags & (FLAGS_COMPONENT_RPT|FLAGS_PROCVSCOMP_RPT)) != 0)
    {
        // Rewind input file.
        //
        n = fseek(in, 0, SEEK_SET);
        if (n != 0)
        {
            ERR("Unable to rewind file \"%s\" for reading (errno=%d). (4)\n", in_name, errno);
            retval = -1;
            goto done;
        }

        //Build a table of component names -> memory status.
        //
        build_component_page_map(in, flags, pa_map, comp_map);

        if ((flags & FLAGS_COMPONENT_RPT) != 0)
        {
            //Generate a report of memory usage for each component.
            //
            generate_component_report(out, flags, comp_map);
        }

        if ((flags & FLAGS_PROCVSCOMP_RPT) != 0)
        {
            //Build component-magnitude-idx <-> component-name look-up tables.
            //
            build_comp_mag_xref(comp_map, mag_comp_xref, comp_mag_xref);

            // Rewind input file.
            //
            n = fseek(in, 0, SEEK_SET);
            if (n != 0)
            {
                ERR("Unable to rewind file \"%s\" for reading (errno=%d). (4)\n", in_name, errno);
                retval = -1;
                goto done;
            }

            //Build the component usage table for each process.
            //
            build_comp_usage_table(in, flags, proc_map, comp_mag_xref);

            //Build a list of pointers to process structures, sort it by decreasing USS...
            //
            for (std::map<std::string, proc_mem_t>::iterator it = proc_map.begin(); it != proc_map.end(); it++)
            {
                proc_list.push_back(&it->second);
            }
            sort(proc_list.begin(), proc_list.end(), proc_mem_uss_gt);

            //..and use it to generate the process vs component memory usage matrix.
            //
            generate_proc_vs_comp_report(out, flags, proc_list, mag_comp_xref);
        }
    }

  done:
    if (out != NULL)
    {
        fclose(out);
    }
    if (in != NULL)
    {
        fclose(in);
    }
    return retval;

}   //main(int, char *[])




