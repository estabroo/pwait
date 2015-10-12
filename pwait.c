/*
 * Copyright Eric Estabrooks 2015
 * Licensed under GPLv2
 * https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 *
 * Parts of this are from http://bewareofgeek.livejournal.com/2945.html (Copyright 2009)
 * https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 *
 * Requires root or cap_net_admin=ep to run
 * 
 *
 */

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <sysexits.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>

#ifndef PWAIT_VERSION
#define PWAIT_VERSION "unknown"
#endif

/* structure for proc list */
typedef struct _proc_entry_ proc_entry_t;
struct _proc_entry_ {
    proc_entry_t* next;
    int           pid;
};

static inline pid_t parse_pid(const char* pid_str)
{
    pid_t pid;
    char* ptr;

    errno = 0;
    pid = (pid_t)strtol(pid_str, &ptr, 0);
    /* set pid to 0 if we didn't convert properly */
    if ((ptr == pid_str) || (*ptr != '\0') || (errno == ERANGE)) {
        pid = 0;
    }
    return pid;
}

static proc_entry_t* create_list(int argc, char* argv[])
{
    int           i;
    pid_t         pid;
    proc_entry_t* head = (proc_entry_t*)NULL;
    proc_entry_t* current = NULL;

    for (i=1; i < argc; i++) {
        pid = parse_pid(argv[i]);
        if (pid < 1) {
            fprintf(stderr, "%s: no such process: %s\n", argv[0], argv[i]);
            break;
        }
        /* use kill signal 0 to check if process exists */
        if (kill(pid, 0) != 0) {
            fprintf(stderr, "%s: no such process: %d\n", argv[0], pid);
            break;
        }
        if (current != NULL) {
            current->next = (proc_entry_t*)malloc(sizeof(proc_entry_t));
            if (current->next == (proc_entry_t*)NULL) {
                perror("malloc");
                break;
            }
            current = current->next;
        } else {
            current = (proc_entry_t*)malloc(sizeof(proc_entry_t));
            if (current == (proc_entry_t*)NULL) {
                perror("malloc");
                break;
            }
            head = current;
        }
        current->pid = pid;
        current->next = (proc_entry_t*)NULL;
    }

    /* free up list if we exited the loop early */
    if (i != argc) {
        current = head;
        while (current != NULL) {
            head = current;
            current = current->next;
            free(head);
        }
        head = NULL;
    }

    return head;
}


/* create a netlink socket */
static int nl_connect()
{
    int rc;
    int nl_sock;
    struct sockaddr_nl sa_nl;

    nl_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (nl_sock == -1) {
        perror("socket");
        return -1;
    }

    sa_nl.nl_family = AF_NETLINK;
    sa_nl.nl_groups = CN_IDX_PROC;
    sa_nl.nl_pid = getpid();

    rc = bind(nl_sock, (struct sockaddr *)&sa_nl, sizeof(sa_nl));
    if (rc == -1) {
        perror("bind");
        close(nl_sock);
        return -1;
    }

    return nl_sock;
}


/* subscribe on proc events (process notifications) */
static int set_proc_ev_listen(int nl_sock, bool enable)
{
    int rc;
    struct __attribute__ ((aligned(NLMSG_ALIGNTO))) {
        struct nlmsghdr nl_hdr;
        struct __attribute__ ((__packed__)) {
            struct cn_msg cn_msg;
            enum proc_cn_mcast_op cn_mcast;
        };
    } nlcn_msg;

    memset(&nlcn_msg, 0, sizeof(nlcn_msg));
    nlcn_msg.nl_hdr.nlmsg_len = sizeof(nlcn_msg);
    nlcn_msg.nl_hdr.nlmsg_pid = getpid();
    nlcn_msg.nl_hdr.nlmsg_type = NLMSG_DONE;

    nlcn_msg.cn_msg.id.idx = CN_IDX_PROC;
    nlcn_msg.cn_msg.id.val = CN_VAL_PROC;
    nlcn_msg.cn_msg.len = sizeof(enum proc_cn_mcast_op);

    nlcn_msg.cn_mcast = enable ? PROC_CN_MCAST_LISTEN : PROC_CN_MCAST_IGNORE;

    rc = send(nl_sock, &nlcn_msg, sizeof(nlcn_msg), 0);
    if (rc == -1) {
        perror("netlink send");
        return -1;
    }

    return 0;
}


static proc_entry_t* update_proc_list(proc_entry_t* head, pid_t pid)
{
    proc_entry_t* prev = head;
    proc_entry_t* current = head;

    while (current != (proc_entry_t*)NULL) {
        if (current->pid == pid) {
            if (prev == current) { /* removing head */
                head = current->next;
                free(current);
            } else {
                prev->next = current->next;
                free(current);
            }
            break;
        }
        prev = current;
        current = current->next;
    }
    return head;
}

static proc_entry_t* check_proc_list(proc_entry_t* head)
{
    proc_entry_t* current = head;
    while (current != (proc_entry_t*)NULL) {
        if (kill(current->pid, 0) != 0) {
            head = update_proc_list(head, current->pid);
            break;
        }
        current = current->next;
    }
    return head;
}
                

/* process events until all requested pids have exited */
static volatile bool need_exit = false;
static int handle_proc_ev(int nl_sock, proc_entry_t* proc_list)
{
    int rc;
    struct __attribute__ ((aligned(NLMSG_ALIGNTO))) {
        struct nlmsghdr nl_hdr;
        struct __attribute__ ((__packed__)) {
            struct cn_msg cn_msg;
            struct proc_event proc_ev;
        };
    } nlcn_msg;

    while (!need_exit) {
        rc = recv(nl_sock, &nlcn_msg, sizeof(nlcn_msg), 0);
        if (rc == 0) {
            /* shutdown? */
            return 0;
        } else if (rc == -1) {
            if (errno == EINTR) continue;
            perror("netlink recv");
            return -1;
        }
        switch (nlcn_msg.proc_ev.what) {
            case PROC_EVENT_FORK: /* missed exit? */
                proc_list = update_proc_list(proc_list, nlcn_msg.proc_ev.event_data.fork.child_pid);
                if (proc_list == (proc_entry_t*)NULL) {
                    need_exit = true;
                }
                break;
            case PROC_EVENT_EXIT:
                proc_list = update_proc_list(proc_list, nlcn_msg.proc_ev.event_data.exit.process_pid);
                if (proc_list == (proc_entry_t*)NULL) {
                    need_exit = true;
                }
                break;
            default: /* general kill check when other messages come through, coredump? */
                proc_list = check_proc_list(proc_list);
                if (proc_list == (proc_entry_t*)NULL) {
                    need_exit = true;
                }
                break;
        }
    }
    return 0;
}


/* sigint handler */
static void on_sigint(int unused)
{
    need_exit = true;
}


static void help(char* prog_path)
{
    char* prog_name = basename(prog_path);

    fprintf(stderr, "pwait version %s\n", PWAIT_VERSION);
    fprintf(stderr, "usage: %s pid [pid ..]\n", prog_name);
    fprintf(stderr, "\twhere pid is the process id of the program your waiting on\n");
    exit(EX_USAGE);
}


int main(int argc, char *argv[])
{
    int           nl_sock;
    int           rc = EX_OK;
    proc_entry_t* proc_list;

    if (argc == 1) {
        help(argv[0]);
    }

    signal(SIGINT, &on_sigint);
    siginterrupt(SIGINT, true);

    nl_sock = nl_connect();
    if (nl_sock == -1)
        exit(EX_NOPERM);

    rc = set_proc_ev_listen(nl_sock, true);
    if (rc != 0) {
        rc = EX_UNAVAILABLE;
        goto out;
    }

    proc_list = create_list(argc, argv);
    if (proc_list != (proc_entry_t*)NULL) {
        rc = handle_proc_ev(nl_sock, proc_list);
        if (rc == -1) {
            rc = EXIT_FAILURE;
            goto out;
        }
    }
    set_proc_ev_listen(nl_sock, false);


out:
    close(nl_sock);
    exit(rc);
}
