/*
 * $Id: ltopas.c,v 1.4 2010-11-15 09:21:15-06 pcinst Exp pcinst $
 *
 * $Log: ltopas.c,v $
 * Revision 1.4  2010-11-15 09:21:15-06  pcinst
 * Cleaned up code and added comments
 *
 * Revision 1.3  2010-11-15 08:24:02-06  pcinst
 * Changed format string that is used to parse /proc/diskstat
 * before the values in diskstat were parsed as signed, now they are
 * parse as unsigned
 * Implemented proc_vmstat
 * ltopas now shows pgpgin/s, pgpgout/s, pgfault/s, and pgmajfault/s metric
 *
 * Revision 1.2  2010-11-13 22:49:46-06  pcinst
 * partitions are now not tracked using two array. That is old_blkio and new_blkio
 * have been removed. Instead one array is used, which is called blkio_list.
 * Benefit of one array for partition is that it can be sorted
 *
 * Revision 1.1  2010-11-11 16:46:05-06  pcinst
 * Initial revision
 *
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>

#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/sysinfo.h>
#include <linux/major.h>
#include <signal.h>
#include <regex.h>
#include <dirent.h>
#include <sys/stat.h>
#include "ltopas.h"

static char rcsid[] = "$Id: ltopas.c,v 1.4 2010-11-15 09:21:15-06 pcinst Exp pcinst $";

struct part_info {
    unsigned int major;  /* Device major number */
    unsigned int minor;  /* Device minor number */
    char name[MAX_NAME_LEN+1];
} partition[MAX_PARTITIONS];

struct blkio_info {
    unsigned int major;                           /* Device major number */
    unsigned int minor;                           /* Device minor number */
    char name [MAX_NAME_LEN+1];
    unsigned long int rd_ios;                          /* Read I/O operations */
    unsigned long int rd_merges;                       /* Reads merged        */
    unsigned long long rd_sectors;                /* Sectors read */
    unsigned long int rd_ticks;  /* Time in queue + service for read */
    unsigned long int wr_ios;  /* Write I/O operations */
    unsigned long int wr_merges;  /* Writes merged */
    unsigned long long wr_sectors; /* Sectors written */
    unsigned long int wr_ticks;  /* Time in queue + service for write */
    unsigned long int ios_pgr;  /* # of IOs in progress; this can be negative (not used) */
    unsigned long int ticks;  /* Time of requests in queue */
    unsigned long int aveq;  /* Average queue length */

    double n_ios;   /* Number of transfers */
    double n_ticks;   /* Total service time */
    double n_kbytes; /* Total kbytes transferred */
    double r_kbytes; /* Total kbytes read */
    double w_kbytes; /* Total kbytes written */
    double busy;   /* Utilization at disk  (percent) */
    double svc_t;   /* Average disk service time */
    double wait;   /* Average wait */
    double size;   /* Average request size */
    double queue;   /* Average queue */
} blkio_list[MAX_PARTITIONS];

double load_avg[3];
struct cpu_info {
    unsigned long long user;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long ctxt;
    long long running;
    long long blocked;
} new_cpu, old_cpu;

struct sysinfo mem;

struct  vm_info_ {
    unsigned long pgpgin; /* Total number of kilobytes the system paged in from disk */
    unsigned long pgpgout; /* Total  number of kilobytes the system paged out to disk */
    unsigned long pgfault; /* Number of page faults (major + minor) made by the system */
    unsigned long pgmajfault; /* Number of major faults the system has made,
                                 those which have required loading a memory page from disk */

} old_vm_info,new_vm_info ;

struct net_info {
    char if_name[32];
    unsigned long long if_ibytes;
    unsigned long long if_obytes;
    double i_kbytes;
    double o_kbytes;
    double n_kbytes;
} new_net[MAX_PARTITIONS], old_net[MAX_PARTITIONS];

boolean quit = FALSE;
FILE *iofp = NULL;      /* /proc/diskstats or /proc/partition */
FILE *cpufp = NULL;      /* /proc/stat */
FILE *netfp = NULL;      /* /proc/net/dev */
FILE *vmfp = NULL;      /* /proc/vmstat */
char *opts = "hN";    /* Options */
char buffer[1024];    /* Temporary buffer for parsing */

unsigned int n_partitions = 0;  /* Number of partitions in data table */
unsigned int ncpu = 0;    /* Number of processors */
unsigned int nnet = 0;    /* Number of netinterface */

char     gr[]="############################                            ";
int interval = 2;
unsigned int count = 0;
int dm_only = FALSE;

char  *legend[] = {
    "Linux topas for host    ***          ***        Interval:      seconds (press q to quit)",
    "",
    "% CPU           (Q (r,b):                    ) NETWORK   KB-In/s  KB-Out/s",
    "Kernel          |                            |",
    "User            |                            |",
    "Wait            |                            |",
    "Idle            |                            |",
    "",
    "MEM ",
    "Pgpgin/s  Pgpgout/s   Fault/s  Majflt/s",
    ""
};

/*
###########################################################################
Helper functions
###########################################################################
*/

boolean should_quit();
void interrupt(int);
void begone();
void handle_error(const char *string, int error);

void print_usage();
void strip_spaces(char *);
char *transform_devmapname(unsigned int, unsigned int, char *);
/**************************************************************************/
/* Draws the output window */
/**************************************************************************/
void dolegend();
/**************************************************************************/
/* Initialize PROC file pointers                                          */
/**************************************************************************/
void proc_init();
/**************************************************************************/
/* prints cpu stats                                                       */
/**************************************************************************/
//void p_cpu(unsigned long long , int , int );
void p_cpu(double , int , int );
void print_cpu_stats();
/**************************************************************************/
/* prints meminfo and vmstat                                              */
/**************************************************************************/
void print_mem_stats();
/**************************************************************************/
/* print disk/partition stats                                             */
/**************************************************************************/
int compare_n_kbytes(const void *p, const void *q);
char *p_partition(unsigned long long, char*);
void print_partition_stats();
/* ######## End of print functions ###################################### */


/**************************************************************************/
/* get cpu stats                                                          */
/**************************************************************************/
void get_number_of_cpus();
void proc_stat();

/**************************************************************************/
/* get memory info and vmstat                                             */
/**************************************************************************/
void proc_meminfo();
void proc_vmstat_init(); /* initialize vm_info structure                  */
void proc_vmstat();

/**************************************************************************/
/* get disk stats */
/**************************************************************************/
void partition_init(char **, int);
void proc_diskstat();

/**************************************************************************/
/* collect netinterface stats */
/**************************************************************************/
void proc_net_init();
void proc_net();
int compare_if_kbytes(const void *p, const void *q);
void print_net_stats();

/**************************************************************************/
/* collect OS metrics
 * This calls the following:
 * proc_stat,    # for cpu stats
 * proc_vmstat,  # for paging stats
 * proc_meminfo, # for Real and Paging space usage
 * proc_diskstat # for disk usage
 * inside the main loop                                                   */
/**************************************************************************/
void get_kernel_stats();

/**************************************************************************/
/* Function implementation                                                */
/**************************************************************************/

/*
 ***************************************************************************
 * Transform device mapper name: Get the user assigned name of the logical
 * device instead of the internal device mapper numbering.
 *
 * IN:
 * @major   Device major number.
 * @minor   Device minor number.
 * @buffer char array to put the device mapper name
 *
 * RETURNS:
 * Assigned name of the logical device.
 ***************************************************************************
 */
char *transform_devmapname(unsigned int major, unsigned int minor, char *buffer)
{
    DIR *dm_dir;
    struct dirent *dp;
    char filen[MAX_FILE_LEN];
    char dummy[] = "N/A";
    char *dm_name = dummy;
    struct stat aux;
    unsigned int dm_major, dm_minor;

    if ((dm_dir = opendir(DEVMAP_DIR)) == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", DEVMAP_DIR, strerror(errno));
        exit(4);
    }

    while ((dp = readdir(dm_dir)) != NULL) {
        /* For each file in DEVMAP_DIR */

        snprintf(filen, MAX_FILE_LEN, "%s/%s", DEVMAP_DIR, dp->d_name);
        filen[MAX_FILE_LEN - 1] = '\0';

        if (stat(filen, &aux) == 0) {
            /* Get its minor and major numbers */

            dm_major = ((aux.st_rdev >> 8) & 0xff);
            dm_minor = (aux.st_rdev & 0xff);

            if ((dm_minor == minor) && (dm_major == major)) {
                dm_name = dp->d_name;
                break;
            }
        }
    }
    closedir(dm_dir);

    strncpy(buffer, dm_name, MAX_NAME_LEN);
    return buffer;
}

/*
 ***************************************************************************
 * ltopas has been interrupted by ctrl-c or kill signal so set quit to TRUE
 ***************************************************************************
 */
void interrupt(int sig) {
    quit = TRUE;

}

/**************************************************************************
 *
 * print functions
 *
 ****************************************************************************/
void dolegend()
{

    int i;
    char host[MAXHOSTNAMELEN + 1];
    for (i = 0; i < sizeof(legend)/ sizeof(char *); i++) {
        mvaddstr(i,0,legend[i]);
    }
    gethostname(host, MAXHOSTNAMELEN);
    i = strlen(host);
    i = 32 - i / 2;
    mvaddstr(0,i,host);
    mvprintw(0,57,"%3d", interval);
    mvprintw(0,57,"%3d", interval);
}

void print_mem_stats()
{
    int row, col, scr_row, scr_col;
    getmaxyx(stdscr, scr_row,scr_col);
    double deltasec = ((new_cpu.user + new_cpu.system +
                new_cpu.idle + new_cpu.iowait) -
            (old_cpu.user + old_cpu.system +
             old_cpu.idle + old_cpu.iowait)) / ncpu / HZ;

    row = 8;
    for(col=0; col < scr_col; col++)
        mvaddch(row,col,' ');

    mvprintw(row, 0, "Mem,MB:%7.1f(%%usd:%4.1f) Swp,MB:%7.1f(%%usd:%4.1f)"
            , (((double) mem.totalram) * mem.mem_unit)/1024.0/1024.0
            , 100 *(1 - ((float) mem.freeram)/mem.totalram)
            , (((double) mem.totalswap) * mem.mem_unit)/1024.0/1024.0
            , 100 *(1 - ((float) mem.freeswap)/mem.totalswap)
            );

    row = 10;
    for(col=0; col < scr_col; col++)
        mvaddch(row,col,' ');
    mvprintw(row,0, "%'9.2f %'9.2f %'9.2f %'9.2f"
            , PER_SEC_VM(new_vm_info.pgpgin, old_vm_info.pgpgin, deltasec)
            , PER_SEC_VM(new_vm_info.pgpgout, old_vm_info.pgpgout, deltasec)
            , PER_SEC_VM(new_vm_info.pgfault, old_vm_info.pgfault, deltasec)
            , PER_SEC_VM(new_vm_info.pgmajfault, old_vm_info.pgmajfault, deltasec));

    old_vm_info = new_vm_info;
} /* end of print_mem_stat */

/* ***************************************************************************
 * print cpu stats
 *************************************************************************** */
//void p_cpu(unsigned long long val, int row, int col)
// Use double instead of unsigned long b/c when <cpu_val>/total
// which is a double is getting converted to unsigned long, we are losing
// the decimal value.
void p_cpu(double val, int row, int col)
{
    int m;
    val*=10;
    // val should be only as big as thousand
    // but sometime wait cpu is this:
    // p_cpu (val=1889067493467440000, row=5, col=8)
    if (val < 0 || val > 1000) {
        val = 0;
    }
    //mvprintw(row,col,"%3d.%1d", val/10, val%10);
    mvprintw(row,col,"%5.1f", val/10);
    m = 1000 - val + 15;
    m = (m * 28) / 1000;
    strncpy(buffer, gr + m, 28); // this line is crashing after 24 hours
    buffer[28] = '\0';
    mvaddstr(row , col+9, buffer);
}

void print_cpu_stats()
{
    double total;
    struct cpu_info cpu;
    int i;
    char temp[256];
    int scr_row, scr_col;


    cpu.user = new_cpu.user - old_cpu.user;
    cpu.system = new_cpu.system - old_cpu.system;
    cpu.idle = new_cpu.idle - old_cpu.idle;
    cpu.iowait = new_cpu.iowait - old_cpu.iowait;
    cpu.ctxt = (new_cpu.ctxt - old_cpu.ctxt)/interval;
    cpu.running = new_cpu.running;
    cpu.blocked = new_cpu.blocked;
    total = (cpu.user + cpu.system + cpu.idle + cpu.iowait) / 100.0;

    getmaxyx(stdscr, scr_row, scr_col);
    for(i=25; i < scr_col; i++)
        mvaddch(1,i,' ');
    mvprintw(1,25, "++ load average : %'.2f , %'.2f , %'.2f ++", load_avg[0],load_avg[1],load_avg[2]);

    for(i=25; i < 47; i++)
        mvaddch(2,i,' ');
    if (cpu.ctxt > 1000UL)
        sprintf(temp,"%5.1fK", cpu.ctxt/1000.0);
    else
        sprintf(temp,"%lld", cpu.ctxt);
    mvprintw(2,25,"%-3lld,%-3lld|ctxt:%7.7s" , cpu.running, cpu.blocked, temp);
    p_cpu( (cpu.system / total),3, 8);
    p_cpu( (cpu.user / total),4, 8);
    p_cpu((cpu.iowait / total),5,8);
    p_cpu((cpu.idle / total), 6,8);
}

/* ***************************************************************************
 * print net stats
 *************************************************************************** */
void p_net(double val, int row, int col)
{

    /* erase the old value */
    mvaddstr(row,col, "        ");
    if (val > 1000)
        mvprintw(row,col,"%5.1fK", val/1000);
    else
        mvprintw(row,col,"%5.1f", val);
}

int compare_if_kbytes(const void *p, const void *q)
{

    return ((struct net_info *) q)->n_kbytes -
        ((struct net_info *) p)->n_kbytes;

}

void print_net_stats()

{
    unsigned int i;
    int row, col, scr_row, scr_col;

    getmaxyx(stdscr, scr_row, scr_col);
    qsort(new_net, nnet, sizeof(struct net_info), compare_if_kbytes);

    for(i=0, row = 3; i < nnet && row < 7; i++, row++) {
        for(col=47; col < scr_col ; col++)
            mvaddch(row, col, ' ');
        mvaddstr(row,47,new_net[i].if_name);
        p_net(new_net[i].i_kbytes, row, 57);
        p_net(new_net[i].o_kbytes, row, 67);

        old_net[i] = new_net[i];
    }
}

/* ***************************************************************************
 * print disk stats
 *************************************************************************** */
char *p_partition(unsigned long long val, char *string)
{
    if (val > 1000.0)
        sprintf(string, "%5llu.%lluK", val/1000, (10* val)%10);
    else
        sprintf(string, "%5llu.%llu", val, (10* val)%10);
    return string;
}

int compare_n_kbytes(const void *p, const void *q)
{

    return ((struct blkio_info *) q)->n_kbytes -
        ((struct blkio_info *) p)->n_kbytes;

}
void print_partition_stats()
{
    int k = 0;
    unsigned int p;
    char temp[1024];
    char temp1[256];
    char temp2[256];
    /* ############################################# */
    /* Following variables are  for displaying TOTAL */
    /* ############################################# */
    double n_ios = 0;   /* Number of transfers */
    double r_kbytes =0; /* Total kbytes read */
    double w_kbytes =0; /* Total kbytes written */
    double busy =0;   /* Utilization at disk  (percent) */
    double svc_t =0;   /* Average disk service time */
    double wait =0;   /* Average wait */
    double queue =0;   /* Average queue */

    int  scr_row = 0, scr_col = 0, col = 0, row = 0;

    qsort(blkio_list, n_partitions, sizeof(struct blkio_info), compare_n_kbytes);

    for (p=0; p < n_partitions; p++) {
        n_ios+=blkio_list[p].n_ios;
        r_kbytes+=blkio_list[p].r_kbytes;
        w_kbytes+=blkio_list[p].w_kbytes;
        queue+=blkio_list[p].queue;
        wait+=blkio_list[p].wait;
        svc_t+=blkio_list[p].svc_t;
        busy+=blkio_list[p].busy;
    }
    if (busy > 100.0)
        busy=100;

    getmaxyx(stdscr, scr_row, scr_col);

    mvprintw(11,0,"KB-Read/s   KB-Write/s    TPS    avgqu-sz  await  svctm   %%util  DISK");
    /* cleanup before printing the row)  */
    for(row = 12; row < scr_row; row++)
        for(col = 0; col < scr_col; col++)
            mvaddch(row, col, ' ');

    row=12;
    sprintf(temp,"%-10s  %-10s",
            p_partition(r_kbytes,temp1),
            p_partition(w_kbytes,temp2));

    mvaddstr(row,0,temp); /* DISK KB-Read/s KB-Write/s */
    mvprintw(row,25,"%5.1f", n_ios);
    mvprintw(row,35,"%5.1f", queue);
    mvprintw(row,43,"%5.1f", wait);
    mvprintw(row,50,"%5.1f", svc_t);
    mvprintw(row,60,"%3.0f", busy);
    mvprintw(row,65,"TOTAL");

    for (p = 0, row=13; p < n_partitions && row < scr_row; p++) {
        if (blkio_list[p].r_kbytes == 0 && blkio_list[p].w_kbytes == 0)
            continue;

        sprintf(temp,"%-10s  %-10s",
                p_partition(blkio_list[p].r_kbytes,temp1),
                p_partition(blkio_list[p].w_kbytes,temp2));

        mvaddstr(row,0,temp); /* DISK KB-Read/s KB-Write/s */
        mvprintw(row,25,"%5.1f", blkio_list[p].n_ios);
        mvprintw(row,35,"%5.1f", blkio_list[p].queue);
        mvprintw(row,43,"%5.1f", blkio_list[p].wait);
        mvprintw(row,50,"%5.1f", blkio_list[p].svc_t);
        mvprintw(row,60,"%3.0f", blkio_list[p].busy);
        for(k =0; k < n_partitions; k++) {
            if (partition[k].major == blkio_list[p].major
                    && partition[k].minor == blkio_list[p].minor) {
                mvprintw(row,65,"%-s", partition[k].name);
            }
        }

        row+=1;

    }

}

boolean should_quit()
{
    int  bytes, scr_row, scr_col,col;
    char buffer[1024 + 1];

    if (quit)
        return(TRUE);

    nodelay(stdscr, TRUE);
    bytes = getch();
    if (bytes == 'q' || bytes == 'Q'){
        return TRUE;
    } else if (bytes == '/') {
        getmaxyx(stdscr,scr_row, scr_col);
        for(col=0; col < scr_col; col++)
            mvaddch(scr_row-1,col,' ');
        curs_set(1);
        echo();
        mvaddstr(scr_row-1,0,"/");
        refresh();
        nodelay(stdscr, FALSE);
        getstr(buffer);
        if(strlen(buffer) > 0) {
            char *regex[1];
            regex[0] = buffer;
            partition_init(&regex[0], 1);
        } else
            partition_init(NULL, 0);

        curs_set(0);
        noecho();
    }

    return(FALSE);
}

void begone()
{
    if (stdscr != NULL)
        endwin();
}


void handle_error(const char *string, int error)
{


    if (error) {
        begone();
        fputs("ltopas: ", stderr);
        if (errno)
            perror(string);
        else
            fprintf(stderr, "%s\n", string);
        exit(EXIT_FAILURE);
    }
}

void strip_spaces(char *s)
{
    char *p;
    int spaced=1;

    p=s;
    for(p=s;*p!=0;p++) {
        if(*p == ':')
            *p=' ';
        if(*p != ' ') {
            *s=*p;
            s++;
            spaced=0;
        } else if(spaced) {
            /* do no thing as this is second space */
        } else {
            *s=*p;
            s++;
            spaced=1;
        }

    }
    *s = 0;
}

/* ***************************************************************************
 * parse meminfo and vmstat
 *************************************************************************** */
void proc_meminfo()
{

    sysinfo(&mem);

}

void proc_vmstat_init()
{
    old_vm_info.pgpgin = 0UL ;
    old_vm_info.pgpgout = 0UL ;
    old_vm_info.pgfault = 0UL ;
    old_vm_info.pgmajfault = 0UL ;

    new_vm_info = old_vm_info;

}

void proc_vmstat()
{
    char line[128];
    rewind(vmfp);

    while(fgets(line, 128, vmfp)) {
        if (!strncmp(line, "pgpgin ", 7)) {
            /* Read number of pages the system paged in */
            sscanf(line + 7, "%lu", &new_vm_info.pgpgin);
        } else if (!strncmp(line, "pgpgout ", 8)) {
            /* Read number of pages the system paged out */
            sscanf(line + 8, "%lu", &new_vm_info.pgpgout);
        } else if (!strncmp(line, "pgfault ", 8)) {
            /* Read number of faults (major+minor) made by the system */
            sscanf(line + 8, "%lu", &new_vm_info.pgfault);
        } else if (!strncmp(line, "pgmajfault ", 11)) {
            /* Read number of faults (major only) made by the system */
            sscanf(line + 11, "%lu", &new_vm_info.pgmajfault);
        }

    } /* end of while loop */

}
/****************************************************************************/
void get_number_of_cpus()
{
    FILE *ncpufp = fopen("/proc/cpuinfo", "r");

    handle_error("Can't open /proc/cpuinfo", !ncpufp);
    while (fgets(buffer, sizeof(buffer), ncpufp)) {
        if (!strncmp(buffer, "processor\t:", 11))
            ncpu++;
    }
    fclose(ncpufp);
    handle_error("Error parsing /proc/cpuinfo", !ncpu);

    old_cpu.user = 0;
    old_cpu.idle = 0;
    old_cpu.system = 0;
    old_cpu.iowait = 0;
    old_cpu.ctxt = 0UL;
}

/************************************************************/
/* count the number of neterinterfaces on the server
   and collect first stats */
/************************************************************/
void proc_net_init()
{
    unsigned long long junk1;
    unsigned long junk;
    int cols = 0;
    nnet = 0;
    char *delim;
    netfp = fopen("/proc/net/dev", "r");
    handle_error("Can't open /proc/net/dev", !netfp);
    char msg[1024];

    /*
       Inter-|   Receive                                                |  Transmit
       face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
lo:    1956      30    0    0    0     0          0         0     1956      30    0    0    0     0       0          0
eth0:       0       0    0    0    0     0          0         0   458718       0  781    0    0     0     781          0
sit0:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0
eth1:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0
*/


    while (fgets(buffer, sizeof(buffer), netfp)) {
        if ( (delim = strchr(buffer, ':')) != NULL) {
            strip_spaces(buffer);
            struct net_info netio;
            if (nnet < MAX_PARTITIONS) {
                cols = sscanf(buffer, "%31s %llu %llu %lu %lu %lu %lu %lu %lu %llu %llu %lu %lu %lu %lu %lu",
                        netio.if_name,
                        &netio.if_ibytes,
                        &junk1,
                        &junk,
                        &junk,
                        &junk,
                        &junk,
                        &junk,
                        &junk,
                        &netio.if_obytes,
                        &junk1,
                        &junk,
                        &junk,
                        &junk,
                        &junk,
                        &junk);

                if (cols != 16) {
                    sprintf(msg,"cannot parse in proc_net_init; token:%d; string: %s", cols, (char *) buffer);
                    handle_error(msg, 1);
                }
                old_net[nnet] = netio;
                new_net[nnet] = netio;
                nnet+=1;
            }
            delim = NULL;
        }
    }
}

/* ***************************************************************************
 * parse netstat
 *************************************************************************** */
void proc_net()
{
    unsigned long long junk1;
    unsigned long junk;
    int cols = 0, i = 0;
    char *delim;
    rewind(netfp);
    char msg[256];
    double deltams = 1000.0 *
        ((new_cpu.user + new_cpu.system +
          new_cpu.idle + new_cpu.iowait) -
         (old_cpu.user + old_cpu.system +
          old_cpu.idle + old_cpu.iowait)) / ncpu / HZ;

    /*
       Inter-|   Receive                                                |  Transmit
       face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
lo:    1956      30    0    0    0     0          0         0     1956      30    0    0    0     0       0          0
eth0:       0       0    0    0    0     0          0         0   458718       0  781    0    0     0     781          0
sit0:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0
eth1:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0
*/


    while (fgets(buffer, sizeof(buffer), netfp)) {
        if ( (delim = strchr(buffer, ':')) != NULL) {
            strip_spaces(buffer);
            struct net_info netio;
            cols = sscanf(buffer, "%31s %llu %llu %lu %lu %lu %lu %lu %lu %llu %llu %lu %lu %lu %lu %lu",
                    netio.if_name,
                    &netio.if_ibytes,
                    &junk1,
                    &junk,
                    &junk,
                    &junk,
                    &junk,
                    &junk,
                    &junk,
                    &netio.if_obytes,
                    &junk1,
                    &junk,
                    &junk,
                    &junk,
                    &junk,
                    &junk);
            if (cols != 16) {
                sprintf(msg,"cannot parse in proc_net_init %d", cols);
                handle_error(msg, 1);
            }

            /* locate netinterface */
            for(i=0; i < nnet; i++) {
                if (strcmp(old_net[i].if_name, netio.if_name) == 0)  {
                    new_net[i] = netio;
                    new_net[i].i_kbytes = PER_SEC((new_net[i].if_ibytes - old_net[i].if_ibytes)/1024.0, deltams);
                    new_net[i].o_kbytes = PER_SEC((new_net[i].if_obytes - old_net[i].if_obytes)/1024.0, deltams);
                    new_net[i].n_kbytes =  new_net[i].i_kbytes + new_net[i].o_kbytes;
                    break;
                }
            }
            delim = NULL;
        }
    }

}

/* Get partition name, major, and minor number */
void partition_init(char **match_list, int n_dev)
{

    n_partitions = 0;
    const char *scan_fmt = NULL;
    unsigned int p;
    char dm_name[MAX_NAME_LEN + 1];

    /* only support 2.6 kernel */
    scan_fmt = "%4d %4d %31s %u";
    handle_error("logic error in partition_initialize()", !scan_fmt);

    rewind(iofp);
    while (fgets(buffer, sizeof(buffer), iofp)) {
        unsigned int reads = 0;
        struct part_info curr;

        if (sscanf(buffer, scan_fmt, &curr.major, &curr.minor,
                    curr.name, &reads) == 4) {

            if(dm_only && curr.major != DEVMAP_MAJOR)
                continue;

            for (p = 0; p < n_partitions
                    && (partition[p].major != curr.major
                        || partition[p].minor != curr.minor);
                    p++);

            if (p == n_partitions && p < MAX_PARTITIONS) {
                if (n_dev) {
                    regex_t regex;
                    int reti, j;
                    for(j=0; j < n_dev && match_list [j]; j++) {
                        /* compile regex */
                        if (regcomp(&regex, match_list[j], REG_EXTENDED) != 0)
                            handle_error("Could not compile regex", 1);
                        if (curr.major == DEVMAP_MAJOR) {
                            reti = regexec(&regex, transform_devmapname(curr.major, curr.minor,curr.name), 0, NULL, 0);
                        } else {
                            reti = regexec(&regex, curr.name, 0, NULL, 0);
                        }

                        /* match */
                        if (!reti) {
                            partition[p] = curr;
                            n_partitions = p + 1;

                            blkio_list[p].major = curr.major;
                            blkio_list[p].minor = curr.minor;
                            blkio_list[p].rd_ios = 0;
                            blkio_list[p].rd_merges = 0;
                            blkio_list[p].rd_sectors = 0;
                            blkio_list[p].rd_ticks = 0;
                            blkio_list[p].wr_ios = 0;
                            blkio_list[p].wr_merges = 0;
                            blkio_list[p].wr_sectors = 0;
                            blkio_list[p].wr_ticks = 0;
                            blkio_list[p].ticks = 0;
                            blkio_list[p].aveq = 0;
                            regfree(&regex);
                            break;
                        }

                        regfree(&regex);
                    }
                } else if (reads) {
                    partition[p] = curr;
                    n_partitions = p + 1;

                    blkio_list[p].major = curr.major;
                    blkio_list[p].minor = curr.minor;
                    blkio_list[p].rd_ios = 0;
                    blkio_list[p].rd_merges = 0;
                    blkio_list[p].rd_sectors = 0;
                    blkio_list[p].rd_ticks = 0;
                    blkio_list[p].wr_ios = 0;
                    blkio_list[p].wr_merges = 0;
                    blkio_list[p].wr_sectors = 0;
                    blkio_list[p].wr_ticks = 0;
                    blkio_list[p].ticks = 0;
                    blkio_list[p].aveq = 0;

                }
            }
        }
    }

    for(p = 0; p < n_partitions; p++) {
        if (partition[p].major == DEVMAP_MAJOR) {
            /*
             * If the device is a device mapper device, try to get its
             * assigned name of its logical device.
             */
            transform_devmapname(partition[p].major, partition[p].minor, dm_name);
            if (strlen(dm_name) > 0) {
                strncpy(partition[p].name, dm_name,MAX_NAME_LEN);
            }
        }
    }

}

/* ***************************************************************************
 * parse stat
 *************************************************************************** */
void proc_stat()
{
    rewind(cpufp);
    new_cpu.ctxt = 0UL;
    new_cpu.running = -1;
    new_cpu.blocked = -1;
    while (fgets(buffer, sizeof(buffer), cpufp)) {
        if (!strncmp(buffer, "cpu ", 4)) {
            int items;
            unsigned long long nice, irq, softirq;

            items = sscanf(buffer,
                    "cpu %llu %llu %llu %llu %llu %llu %llu",
                    &new_cpu.user, &nice,
                    &new_cpu.system,
                    &new_cpu.idle,
                    &new_cpu.iowait,
                    &irq, &softirq);

            new_cpu.user += nice;
            if (items == 4)
                new_cpu.iowait = 0;
            if (items == 7)
                new_cpu.system += irq + softirq;

        }
        sscanf(buffer,"ctxt %llu", &new_cpu.ctxt);
        sscanf(buffer,"procs_running %lld", &new_cpu.running);
        sscanf(buffer,"procs_blocked %lld", &new_cpu.blocked);
    }
    if (getloadavg(load_avg, 3) == -1) {
        load_avg[0] = load_avg[1] = load_avg[2] = 0;
    }


}

/* ***************************************************************************
 * parse diskstats
 *************************************************************************** */
void proc_diskstat()
{
    /* blkio_c holds partition that is being parsed/process
     * blkio_d holds the delta
     * */
    struct blkio_info blkio_c, blkio_d;
    unsigned int p;

    double deltams = 1000.0 *
        ((new_cpu.user + new_cpu.system +
          new_cpu.idle + new_cpu.iowait) -
         (old_cpu.user + old_cpu.system +
          old_cpu.idle + old_cpu.iowait)) / ncpu / HZ;

    /*
     * Sample /proc/diskstats ######################################################################
     major minor  #blocks  name     rio rmerge rsect ruse wio wmerge wsect wuse running use aveq

     33     0    1052352 hde 2855 15 2890 4760 0 0 0 0 -4 7902400 11345292
     33     1    1050304 hde1 2850 0 2850 3930 0 0 0 0 0 3930 3930
     3     0   39070080 hda 9287 19942 226517 90620 8434 25707 235554 425790 -12 7954830 33997658
     3     1   31744408 hda1 651 90 5297 2030 0 0 0 0 0 2030 2030
     3     2    6138720 hda2 7808 19561 218922 79430 7299 20529 222872 241980 0 59950 321410
     3     3     771120 hda3 13 41 168 80 0 0 0 0 0 80 80
     3     4          1 hda4 0 0 0 0 0 0 0 0 0 0 0
     3     5     408208 hda5 812 241 2106 9040 1135 5178 12682 183810 0 11230 192850
     * Sample /proc/diskstats ######################################################################
     */

    rewind(iofp);
    int items;
    while (fgets(buffer, sizeof(buffer), iofp)) {
        /* only support 2.6 kernel */
        /* major minor name rio rmerge rsect ruse wio wmerge wsect wuse running use aveq */
        items = sscanf(buffer, "%u %u %31s %lu %lu %llu %lu %lu %lu %llu %lu %lu %lu %lu",
                &blkio_c.major, &blkio_c.minor,  blkio_c.name,
                &blkio_c.rd_ios, &blkio_c.rd_merges,
                &blkio_c.rd_sectors, &blkio_c.rd_ticks,
                &blkio_c.wr_ios, &blkio_c.wr_merges,
                &blkio_c.wr_sectors, &blkio_c.wr_ticks,
                &blkio_c.ios_pgr, &blkio_c.ticks, &blkio_c.aveq);
        if (items < 14) {
            sprintf(buffer, "read error in proc_diskstat;read items %d\n", items);
            handle_error(buffer, TRUE);
        }

        /* ##############################
         * Locate partition in data table
         * b/c in partition_init we figured
         * out which partitions we should
         * display
         * ############################## */
        for (p = 0; p < n_partitions; p++) {
            if (partition[p].major == blkio_c.major
                    && partition[p].minor == blkio_c.minor)
                break;
        }
        if (p == n_partitions)
            continue; /* skip this partition */

        /* find this partition on partition array/list */
        for (p = 0; p < n_partitions; p++) {
            if (blkio_list[p].major == blkio_c.major
                    && blkio_list[p].minor == blkio_c.minor) {

                blkio_d.rd_ios = blkio_c.rd_ios
                    - blkio_list[p].rd_ios;
                blkio_d.rd_merges = blkio_c.rd_merges
                    - blkio_list[p].rd_merges;
                blkio_d.rd_sectors = blkio_c.rd_sectors
                    - blkio_list[p].rd_sectors;
                blkio_d.rd_ticks = blkio_c.rd_ticks
                    - blkio_list[p].rd_ticks;
                blkio_d.wr_ios = blkio_c.wr_ios
                    - blkio_list[p].wr_ios;
                blkio_d.wr_merges = blkio_c.wr_merges
                    - blkio_list[p].wr_merges;
                blkio_d.wr_sectors = blkio_c.wr_sectors
                    - blkio_list[p].wr_sectors;
                blkio_d.wr_ticks = blkio_c.wr_ticks
                    - blkio_list[p].wr_ticks;
                blkio_d.ticks = blkio_c.ticks
                    - blkio_list[p].ticks;
                blkio_d.aveq = blkio_c.aveq
                    - blkio_list[p].aveq;

                /* save the current value in blkio_list after getting delta */
                blkio_list[p] = blkio_c;


                /* now figure out rate = delta/sec */
                blkio_list[p].n_ios  = PER_SEC(blkio_d.rd_ios + blkio_d.wr_ios, deltams);
                blkio_list[p].n_ticks = blkio_d.rd_ticks + blkio_d.wr_ticks;
                blkio_list[p].n_kbytes = PER_SEC((blkio_d.rd_sectors + blkio_d.wr_sectors) / 2.0, deltams);
                blkio_list[p].r_kbytes = PER_SEC(blkio_d.rd_sectors / 2.0, deltams);
                blkio_list[p].w_kbytes = PER_SEC(blkio_d.wr_sectors / 2.0, deltams);

                blkio_list[p].queue = blkio_d.aveq / deltams;
                blkio_list[p].size = blkio_list[p].n_ios ? blkio_list[p].n_kbytes / blkio_list[p].n_ios : 0;
                blkio_list[p].wait = blkio_list[p].n_ios ? blkio_list[p].n_ticks /  blkio_list[p].n_ios : 0;
                blkio_list[p].svc_t = blkio_list[p].n_ios ? blkio_d.ticks / blkio_list[p].n_ios : 0;
                blkio_list[p].busy = 100.0 * blkio_d.ticks / deltams; /* percentage! */
                if (blkio_list[p].busy > 100.0)
                    blkio_list[p].busy = 100;

                break;
            } /* end of if statement; only process a partition if it is in data table */
        } /* end of for loop that looks up partition in data table */
    } /* end of while loop that reads /proc/diskstat */

} /* end of proc_diskstat */

/* ***************************************************************************
 * parse stats, diskstats, netstat, meminfo, and vmstat
 *************************************************************************** */
void get_kernel_stats()
{

    proc_stat();
    proc_diskstat();
    proc_net();
    proc_meminfo();
    proc_vmstat();

}


void print_usage()
{
    fputs("Usage: ltopas [disks...] [interval [count]]\n"
            "       Disks are treated as regular expression\n",
            stderr);
    fprintf(stderr, "** By IGS DBAs compiled on %s at %s\n", __DATE__, __TIME__);
    fprintf(stderr, "   %s\n", rcsid);
    exit(EXIT_SUCCESS);
}

/* ***************************************************************************
 * Initialize proc file pointer
 *************************************************************************** */
void proc_init()
{
    get_number_of_cpus();

    iofp = fopen("/proc/diskstats", "r");
    handle_error("Can't get I/O statistics on this system", !iofp);

    cpufp = fopen("/proc/stat", "r");
    handle_error("Can't open /proc/stat", !cpufp);

    vmfp = fopen("/proc/vmstat", "r");
    handle_error("Can't open /proc/vmstat", !vmfp);
    proc_vmstat_init(); /* initialize vm_info struct to 0 */

    proc_net_init(); /* initialize net struct to 0 */

}

/**************************************************************************
 *
 * Main
 *
 ****************************************************************************/

int main(int argc, char *argv[])
{
    int c,i;
    unsigned int  n_dev ;
    signal(SIGINT,  interrupt);
    signal(SIGTERM, interrupt);
    signal(SIGQUIT, interrupt);

    /*******************************************************************/
    /* Open proc files                                                 */
    /*******************************************************************/
    proc_init();

    /* parse argv */
    while ((c = getopt(argc, argv, opts)) != EOF) {
        switch (c) {
            case 'N':
                dm_only=TRUE;
                break;
            case 'h':
            default:
                print_usage();
        }
    }

    /* List of disks/devices [delay [count]]. */
    for (n_dev = 0; optind + n_dev < argc
            && !isdigit(argv[optind + n_dev][0]); n_dev++);

    partition_init(&argv[optind], n_dev);
    optind += n_dev;

    switch(argc - optind) {
        case 2:
            count = atoi(argv[optind + 1]);
            /* drop down */
        case 1:
            interval = atoi(argv[optind]);
            break;
        case 0:
            count = -1;
            break;
        default:
            print_usage();
    }

    /*******************************************************************/
    /* Start Curses Mode                                               */
    /*******************************************************************/
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    dolegend();

    //for (i=0; i < count ; i++) {
    for (;;) {
        if(should_quit())
            break;
        get_kernel_stats();
        print_cpu_stats();
        print_mem_stats();
        print_partition_stats();
        print_net_stats();
        refresh();

        old_cpu = new_cpu; /* save cpu stats in main b/c new and old cpu
                              jiffies are used to figure elapaed time
                              in proc_diskstat, proc_netstat, proc_vmstat, etc
                              */

        sleep(interval);
    }

    begone();
    printf("\nExiting ...\n");
    exit(EXIT_SUCCESS);

} /* End of Main */
