/* Copyright (C) 2015-2021, Wazuh Inc.
 * Copyright (C) 2009 Trend Micro Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

/* Syscheck
 * Copyright (C) 2003 Daniel B. Cid <daniel@underlinux.com.br>
 */
#include "shared.h"
#include "syscheck.h"
#include "rootcheck/rootcheck.h"

#ifndef WIN32

// LCOV_EXCL_START

/* Print help statement */
__attribute__((noreturn)) static void help_syscheckd()
{
    print_header();
    print_out("  %s: -[Vhdtf] [-c config]", ARGV0);
    print_out("    -V          Version and license message");
    print_out("    -h          This help message");
    print_out("    -d          Execute in debug mode. This parameter");
    print_out("                can be specified multiple times");
    print_out("                to increase the debug level.");
    print_out("    -t          Test configuration");
    print_out("    -f          Run in foreground");
    print_out("    -c <config> Configuration file to use (default: %s)", OSSECCONF);
    print_out(" ");
    exit(1);
}

/* Syscheck unix main */
int main(int argc, char **argv)
{
    int c, r;
    int debug_level = 0;
    int test_config = 0, run_foreground = 0;
    const char *cfg = OSSECCONF;
    gid_t gid;
    const char *group = GROUPGLOBAL;
    directory_t *dir_it = NULL;

    /* Set the name */
    OS_SetName(ARGV0);

    char * home_path = w_homedir(argv[0]);

    while ((c = getopt(argc, argv, "Vtdhfc:")) != -1) {
        switch (c) {
            case 'V':
                print_version();
                break;
            case 'h':
                help_syscheckd();
                break;
            case 'd':
                nowDebug();
                debug_level ++;
                break;
            case 'f':
                run_foreground = 1;
                break;
            case 'c':
                if (!optarg) {
                    merror_exit("-c needs an argument");
                }
                cfg = optarg;
                break;
            case 't':
                test_config = 1;
                break;
            default:
                help_syscheckd();
                break;
        }
    }

    /* Change current working directory */
    if (chdir(home_path) == -1) {
        merror_exit(CHDIR_ERROR, home_path, errno, strerror(errno));
    }

    mdebug1(WAZUH_HOMEDIR, home_path);
    os_free(home_path);

    /* Check if the group given is valid */
    gid = Privsep_GetGroup(group);
    if (gid == (gid_t) - 1) {
        merror_exit(USER_ERROR, "", group, strerror(errno), errno);
    }

    /* Privilege separation */
    if (Privsep_SetGroup(gid) < 0) {
        merror_exit(SETGID_ERROR, group, errno, strerror(errno));
    }

    /* Read internal options */
    read_internal(debug_level);

    /* Check if the configuration is present */
    if (File_DateofChange(cfg) < 0) {
        merror_exit(NO_CONFIG, cfg);
    }

    /* Read syscheck config */
    if ((r = Read_Syscheck_Config(cfg)) < 0) {
        merror(RCONFIG_ERROR, SYSCHECK, cfg);
        syscheck.disabled = 1;
    } else if ((r == 1) || (syscheck.disabled == 1)) {
        if (syscheck.directories == NULL || syscheck.directories[0] == NULL) {
            if (!test_config) {
                minfo(FIM_DIRECTORY_NOPROVIDED);
            }
        }

        if (!syscheck.ignore) {
            os_calloc(1, sizeof(char *), syscheck.ignore);
        } else {
            os_free(syscheck.ignore[0]);
        }

        if (!test_config) {
            minfo(FIM_DISABLED);
        }
    }

    /* Rootcheck config */
    if (rootcheck_init(test_config) == 0) {
        syscheck.rootcheck = 1;
    } else {
        syscheck.rootcheck = 0;
    }

    /* Exit if testing config */
    if (test_config) {
        exit(0);
    }

    /* Setup libmagic */
#ifdef USE_MAGIC
    init_magic(&magic_cookie);
#endif

    if (!run_foreground) {
        nowDaemon();
        goDaemon();
    }

    /* Start signal handling */
    StartSIG(ARGV0);

    // Start com request thread
    w_create_thread(syscom_main, NULL);

    /* Create pid */
    if (CreatePID(ARGV0, getpid()) < 0) {
        merror_exit(PID_ERROR);
    }

    if (syscheck.rootcheck) {
        rootcheck_connect();
    }

    /* Connect to the queue */

    if ((syscheck.queue = StartMQ(DEFAULTQUEUE, WRITE, INFINITE_OPENQ_ATTEMPTS)) < 0) {
        merror_exit(QUEUE_FATAL, DEFAULTQUEUE);
    }

    if (!syscheck.disabled) {
        /* Start up message */
        minfo(STARTUP_MSG, (int)getpid());

        /* Print directories to be monitored */
        foreach_array(dir_it, syscheck.directories) {
            char optstr[ 1024 ];

            if (dir_it->symbolic_links == NULL) {
                minfo(FIM_MONITORING_DIRECTORY, dir_it->path,
                      syscheck_opts2str(optstr, sizeof(optstr), dir_it->options));
            } else {
                minfo(FIM_MONITORING_LDIRECTORY, dir_it->path, dir_it->symbolic_links,
                      syscheck_opts2str(optstr, sizeof(optstr), dir_it->options));
            }

            if (dir_it->tag != NULL)
                mdebug1(FIM_TAG_ADDED, dir_it->tag, dir_it->path);

            // Print diff file size limit
            if ((dir_it->options & CHECK_SEECHANGES) && syscheck.file_size_enabled) {
                mdebug2(FIM_DIFF_FILE_SIZE_LIMIT, dir_it->diff_size_limit, dir_it->path);
            }
        }

        if (!syscheck.file_size_enabled) {
            minfo(FIM_FILE_SIZE_LIMIT_DISABLED);
        }

        // Print maximum disk quota to be used by the queue/diff/local folder
        if (syscheck.disk_quota_enabled) {
            mdebug2(FIM_DISK_QUOTA_LIMIT, syscheck.disk_quota_limit);
        }
        else {
            minfo(FIM_DISK_QUOTA_LIMIT_DISABLED);
        }

        /* Print ignores. */
        if(syscheck.ignore)
            for (r = 0; syscheck.ignore[r] != NULL; r++)
                minfo(FIM_PRINT_IGNORE_ENTRY, "file", syscheck.ignore[r]);

        /* Print sregex ignores. */
        if(syscheck.ignore_regex)
            for (r = 0; syscheck.ignore_regex[r] != NULL; r++)
                minfo(FIM_PRINT_IGNORE_SREGEX, "file", syscheck.ignore_regex[r]->raw);

        /* Print files with no diff. */
        if (syscheck.nodiff){
            r = 0;
            while (syscheck.nodiff[r] != NULL) {
                minfo(FIM_NO_DIFF, syscheck.nodiff[r]);
                r++;
            }
        }

        /* Check directories set for real time */
        foreach_array(dir_it, syscheck.directories) {
            if (dir_it->options & REALTIME_ACTIVE) {
#if defined (INOTIFY_ENABLED) || defined (WIN32)
                minfo(FIM_REALTIME_MONITORING_DIRECTORY, dir_it->path);
#else
                mwarn(FIM_WARN_REALTIME_DISABLED, dir_it->path);
                dir_it->options &= ~REALTIME_ACTIVE;
                dir_it->options |= SCHEDULED_ACTIVE;
#endif
            }
        }
    }

    fim_initialize();

    // Audit events thread
    if (!syscheck.disabled && syscheck.enable_whodata) {
#ifdef ENABLE_AUDIT
        if (audit_init() < 0) {
            directory_t *dir_it;

            mwarn(FIM_WARN_AUDIT_THREAD_NOSTARTED);

            // Switch who-data to real-time mode

            foreach_array(dir_it, syscheck.directories) {
                if (dir_it->options & WHODATA_ACTIVE) {
                    dir_it->options &= ~WHODATA_ACTIVE;
                    dir_it->options |= REALTIME_ACTIVE;
                }
            }
        }
#else
        merror(FIM_ERROR_WHODATA_AUDIT_SUPPORT);
#endif
    }

    /* Start the daemon */
    start_daemon();

    // We shouldn't reach this point unless syscheck is disabled
    while(1) {
        pause();
    }

    return (0);
}

#endif /* !WIN32 */

// LCOV_EXCL_STOP
