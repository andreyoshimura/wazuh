/*
 * Copyright (C) 2015-2021, Wazuh Inc.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

#include "../wrappers/common.h"
#include "../wrappers/posix/stat_wrappers.h"
#include "../wrappers/linux/inotify_wrappers.h"
#include "../wrappers/wazuh/shared/debug_op_wrappers.h"
#include "../wrappers/wazuh/shared/file_op_wrappers.h"
#include "../wrappers/wazuh/shared/mq_op_wrappers.h"
#include "../wrappers/wazuh/shared/randombytes_wrappers.h"
#include "../wrappers/wazuh/syscheckd/create_db_wrappers.h"
#include "../wrappers/wazuh/syscheckd/fim_db_wrappers.h"
#include "../wrappers/wazuh/syscheckd/run_realtime_wrappers.h"
#include "../wrappers/wazuh/syscheckd/win_whodata_wrappers.h"

#include "../syscheckd/syscheck.h"
#include "../syscheckd/db/fim_db.h"

#ifdef TEST_WINAGENT
#include "../wrappers/windows/processthreadsapi_wrappers.h"

void set_priority_windows_thread();
void set_whodata_mode_changes();
#endif

/* External 'static' functions prototypes */
void fim_send_msg(char mq, const char * location, const char * msg);

#ifndef TEST_WINAGENT
void fim_link_update(const char *new_path, directory_t *configuration);
void fim_link_check_delete(directory_t *configuration);
void fim_link_delete_range(const directory_t *configuration);
void fim_link_silent_scan(char *path, directory_t *configuration);
void fim_link_reload_broken_link(char *path, directory_t *configuration);
void fim_delete_realtime_watches(const directory_t *configuration);
#endif

extern time_t last_time;
extern unsigned int files_read;

/* redefinitons/wrapping */

#ifdef TEST_WINAGENT
int __wrap_audit_restore(void) {
    return mock();
}
#else
time_t __wrap_time(time_t *timer) {
    return mock_type(time_t);
}

#endif

/* Setup/Teardown */

static int setup_group(void ** state) {
#ifdef TEST_WINAGENT
    expect_string(__wrap__mdebug1, formatted_msg, "(6287): Reading configuration file: 'test_syscheck.conf'");
    expect_string(__wrap__mdebug1, formatted_msg, "Found ignore regex node .log$|.htm$|.jpg$|.png$|.chm$|.pnf$|.evtx$|.swp$");
    expect_string(__wrap__mdebug1, formatted_msg, "Found ignore regex node .log$|.htm$|.jpg$|.png$|.chm$|.pnf$|.evtx$|.swp$ OK?");
    expect_string(__wrap__mdebug1, formatted_msg, "Found ignore regex size 0");
    expect_string(__wrap__mdebug1, formatted_msg, "Found nodiff regex node ^file");
    expect_string(__wrap__mdebug1, formatted_msg, "Found nodiff regex node ^file OK?");
    expect_string(__wrap__mdebug1, formatted_msg, "Found nodiff regex size 0");
    expect_string(__wrap__mdebug1, formatted_msg, "Found nodiff regex node test_$");
    expect_string(__wrap__mdebug1, formatted_msg, "Found nodiff regex node test_$ OK?");
    expect_string(__wrap__mdebug1, formatted_msg, "Found nodiff regex size 1");
#else
    expect_string(__wrap__mdebug1, formatted_msg, "(6287): Reading configuration file: 'test_syscheck.conf'");
    expect_string(__wrap__mdebug1, formatted_msg, "Found ignore regex node .log$|.swp$");
    expect_string(__wrap__mdebug1, formatted_msg, "Found ignore regex node .log$|.swp$ OK?");
    expect_string(__wrap__mdebug1, formatted_msg, "Found ignore regex size 0");
    expect_string(__wrap__mdebug1, formatted_msg, "Found nodiff regex node ^file");
    expect_string(__wrap__mdebug1, formatted_msg, "Found nodiff regex node ^file OK?");
    expect_string(__wrap__mdebug1, formatted_msg, "Found nodiff regex size 0");

    syscheck.database = fim_db_init(FIM_DB_DISK);
#endif

#if defined(TEST_AGENT) || defined(TEST_WINAGENT)
    expect_string(__wrap__mdebug1, formatted_msg, "(6208): Reading Client Configuration [test_syscheck.conf]");
#endif

    will_return_always(__wrap_os_random, 12345);

    if(Read_Syscheck_Config("test_syscheck.conf"))
        fail();

    syscheck.realtime = (rtfim *) calloc(1, sizeof(rtfim));
    if(syscheck.realtime == NULL) {
        return -1;
    }
#ifndef TEST_WINAGENT
    will_return(__wrap_time, 1);
#endif
    syscheck.realtime->dirtb = OSHash_Create();
    if (syscheck.realtime->dirtb == NULL) {
        return -1;
    }

    OSHash_Add_ex(syscheck.realtime->dirtb, "key", strdup("data"));

#ifdef TEST_WINAGENT
    time_mock_value = 1;
#endif
    return 0;
}

#ifndef TEST_WINAGENT

static int setup_symbolic_links(void **state) {
    if (syscheck.directories[1]->path != NULL) {
        free(syscheck.directories[1]->path);
        syscheck.directories[1]->path = NULL;
    }

    syscheck.directories[1]->path = strdup("/link");
    syscheck.directories[1]->symbolic_links = strdup("/folder");
    syscheck.directories[1]->options |= REALTIME_ACTIVE;

    if (syscheck.directories[1]->path == NULL || syscheck.directories[1]->symbolic_links == NULL) {
        return -1;
    }

    return 0;
}

static int teardown_symbolic_links(void **state) {
    if (syscheck.directories[1]->path != NULL) {
        free(syscheck.directories[1]->path);
        syscheck.directories[1]->path = NULL;
    }

    if (syscheck.directories[1]->symbolic_links != NULL) {
        free(syscheck.directories[1]->symbolic_links);
        syscheck.directories[1]->symbolic_links = NULL;
    }

    syscheck.directories[1]->path = strdup("/etc");
    syscheck.directories[1]->options &= ~REALTIME_ACTIVE;

    if (syscheck.directories[1]->path == NULL) {
        return -1;
    }

    return 0;
}

static int setup_tmp_file(void **state) {
    fim_tmp_file *tmp_file = calloc(1, sizeof(fim_tmp_file));
    tmp_file->elements = 1;

    if (setup_symbolic_links(NULL) < 0) {
        return -1;
    }

    *state = tmp_file;

    return 0;
}

static int teardown_tmp_file(void **state) {
    fim_tmp_file *tmp_file = *state;
    free(tmp_file);

    if (teardown_symbolic_links(NULL) < 0) {
        return -1;
    }

    return 0;
}

#endif

static int teardown_group(void **state) {
#ifdef TEST_WINAGENT
    if (syscheck.realtime) {
        if (syscheck.realtime->dirtb) {
            OSHash_Free(syscheck.realtime->dirtb);
        }
        free(syscheck.realtime);
        syscheck.realtime = NULL;
    }
#endif

    fim_db_clean();
    Free_Syscheck(&syscheck);

    return 0;
}

/**
 * @brief This function loads expect and will_return calls for the function send_sync_msg
*/
static void expect_w_send_sync_msg(const char *msg, const char *locmsg, char location, int ret) {
    expect_SendMSG_call(msg, locmsg, location, ret);
}

static int setup_max_fps(void **state) {
    syscheck.max_files_per_second = 1;
    return 0;
}

static int teardown_max_fps(void **state) {
    syscheck.max_files_per_second = 0;
    return 0;
}

/* tests */

void test_fim_whodata_initialize(void **state)
{
    int ret;
#ifdef TEST_WINAGENT
    int i;
    char *dirs[] = {
        "%WINDIR%\\System32\\WindowsPowerShell\\v1.0",
        NULL
    };
    char expanded_dirs[1][OS_SIZE_1024];

    // Expand directories
    for(i = 0; dirs[i]; i++) {
        if(!ExpandEnvironmentStrings(dirs[i], expanded_dirs[i], OS_SIZE_1024))
            fail();

        str_lowercase(expanded_dirs[i]);
        expect_realtime_adddir_call(expanded_dirs[i], 0);
    }
    will_return(__wrap_run_whodata_scan, 0);
    will_return(wrap_CreateThread, (HANDLE)123456);
#endif

    ret = fim_whodata_initialize();

    assert_int_equal(ret, 0);
}

void test_log_realtime_status(void **state)
{
    (void) state;

    log_realtime_status(2);

    expect_string(__wrap__minfo, formatted_msg, FIM_REALTIME_STARTED);
    log_realtime_status(1);
    log_realtime_status(1);

    expect_string(__wrap__minfo, formatted_msg, FIM_REALTIME_PAUSED);
    log_realtime_status(2);
    log_realtime_status(2);

    expect_string(__wrap__minfo, formatted_msg, FIM_REALTIME_RESUMED);
    log_realtime_status(1);
}

void test_fim_send_msg(void **state) {
    (void) state;

    expect_w_send_sync_msg("test", SYSCHECK, SYSCHECK_MQ, 0);
    fim_send_msg(SYSCHECK_MQ, SYSCHECK, "test");
}

void test_fim_send_msg_retry(void **state) {
    (void) state;

    expect_w_send_sync_msg("test", SYSCHECK, SYSCHECK_MQ, -1);

    expect_string(__wrap__merror, formatted_msg, QUEUE_SEND);

    expect_StartMQ_call(DEFAULTQUEUE, WRITE, 0);

    expect_w_send_sync_msg("test", SYSCHECK, SYSCHECK_MQ, -1);

    fim_send_msg(SYSCHECK_MQ, SYSCHECK, "test");
}

void test_fim_send_msg_retry_error(void **state) {
    (void) state;

    expect_w_send_sync_msg("test", SYSCHECK, SYSCHECK_MQ, -1);
    expect_string(__wrap__merror, formatted_msg, QUEUE_SEND);

    expect_StartMQ_call(DEFAULTQUEUE, WRITE, -1);

    expect_string(__wrap__merror_exit, formatted_msg, "(1211): Unable to access queue: 'queue/sockets/queue'. Giving up.");

    // This code shouldn't run
    expect_w_send_sync_msg("test", SYSCHECK, SYSCHECK_MQ, -1);

    fim_send_msg(SYSCHECK_MQ, SYSCHECK, "test");
}

#ifdef TEST_WINAGENT

void test_fim_whodata_initialize_fail_set_policies(void **state)
{
    int ret;
    int i;
    char *dirs[] = {
        "%WINDIR%\\System32\\WindowsPowerShell\\v1.0",
        NULL
    };
    char expanded_dirs[1][OS_SIZE_1024];

    // Expand directories
    for(i = 0; dirs[i]; i++) {
        if(!ExpandEnvironmentStrings(dirs[i], expanded_dirs[i], OS_SIZE_1024))
            fail();

        str_lowercase(expanded_dirs[i]);
        expect_realtime_adddir_call(expanded_dirs[i], 0);
    }

    will_return(__wrap_run_whodata_scan, 1);
    expect_string(__wrap__merror, formatted_msg,
      "(6710): Failed to start the Whodata engine. Directories/files will be monitored in Realtime mode");

    will_return(__wrap_audit_restore, NULL);

    ret = fim_whodata_initialize();

    assert_int_equal(ret, -1);
}

void test_set_priority_windows_thread_highest(void **state) {
    syscheck.process_priority = -10;

    expect_string(__wrap__mdebug1, formatted_msg, "(6320): Setting process priority to: '-10'");

    will_return(wrap_GetCurrentThread, (HANDLE)123456);

    expect_SetThreadPriority_call((HANDLE)123456, THREAD_PRIORITY_HIGHEST, true);

    set_priority_windows_thread();
}

void test_set_priority_windows_thread_above_normal(void **state) {
    syscheck.process_priority = -8;

    expect_string(__wrap__mdebug1, formatted_msg, "(6320): Setting process priority to: '-8'");

    will_return(wrap_GetCurrentThread, (HANDLE)123456);
    expect_SetThreadPriority_call((HANDLE)123456, THREAD_PRIORITY_ABOVE_NORMAL, true);

    set_priority_windows_thread();
}

void test_set_priority_windows_thread_normal(void **state) {
    syscheck.process_priority = 0;

    expect_string(__wrap__mdebug1, formatted_msg, "(6320): Setting process priority to: '0'");

    will_return(wrap_GetCurrentThread, (HANDLE)123456);
    expect_SetThreadPriority_call((HANDLE)123456, THREAD_PRIORITY_NORMAL, true);

    set_priority_windows_thread();
}

void test_set_priority_windows_thread_below_normal(void **state) {
    syscheck.process_priority = 2;

    expect_string(__wrap__mdebug1, formatted_msg, "(6320): Setting process priority to: '2'");

    will_return(wrap_GetCurrentThread, (HANDLE)123456);
    expect_SetThreadPriority_call((HANDLE)123456, THREAD_PRIORITY_BELOW_NORMAL, true);

    set_priority_windows_thread();
}

void test_set_priority_windows_thread_lowest(void **state) {
    syscheck.process_priority = 7;

    expect_string(__wrap__mdebug1, formatted_msg, "(6320): Setting process priority to: '7'");

    will_return(wrap_GetCurrentThread, (HANDLE)123456);
    expect_SetThreadPriority_call((HANDLE)123456, THREAD_PRIORITY_LOWEST, true);

    set_priority_windows_thread();
}

void test_set_priority_windows_thread_idle(void **state) {
    syscheck.process_priority = 20;

    expect_string(__wrap__mdebug1, formatted_msg, "(6320): Setting process priority to: '20'");

    will_return(wrap_GetCurrentThread, (HANDLE)123456);
    expect_SetThreadPriority_call((HANDLE)123456, THREAD_PRIORITY_IDLE, true);

    set_priority_windows_thread();
}

void test_set_priority_windows_thread_error(void **state) {
    syscheck.process_priority = 10;

    expect_string(__wrap__mdebug1, formatted_msg, "(6320): Setting process priority to: '10'");

    will_return(wrap_GetCurrentThread, (HANDLE)123456);
    expect_SetThreadPriority_call((HANDLE)123456, THREAD_PRIORITY_LOWEST, false);

    will_return(wrap_GetLastError, 2345);

    expect_string(__wrap__merror, formatted_msg, "Can't set thread priority: 2345");

    set_priority_windows_thread();
}

#ifdef WIN_WHODATA
void test_set_whodata_mode_changes(void **state) {
    int i;
    char *dirs[] = {
        "%PROGRAMDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup",
        "%WINDIR%\\System32\\drivers\\etc",
        "%WINDIR%\\System32\\wbem",
        NULL
    };
    char expanded_dirs[3][OS_SIZE_1024];

    // Mark directories to be added in realtime
    syscheck.directories[0]->dirs_status.status |= WD_CHECK_REALTIME;
    syscheck.directories[0]->dirs_status.status &= ~WD_CHECK_WHODATA;
    syscheck.directories[7]->dirs_status.status |= WD_CHECK_REALTIME;
    syscheck.directories[7]->dirs_status.status &= ~WD_CHECK_WHODATA;
    syscheck.directories[8]->dirs_status.status |= WD_CHECK_REALTIME;
    syscheck.directories[8]->dirs_status.status &= ~WD_CHECK_WHODATA;

    // Expand directories
    for(i = 0; dirs[i]; i++) {
        if(!ExpandEnvironmentStrings(dirs[i], expanded_dirs[i], OS_SIZE_1024))
            fail();

        str_lowercase(expanded_dirs[i]);
        expect_realtime_adddir_call(expanded_dirs[i], i % 2 == 0);
    }

    expect_string(__wrap__mdebug1, formatted_msg, "(6225): The 'c:\\programdata\\microsoft\\windows\\start menu\\programs\\startup' directory starts to be monitored in real-time mode.");
    expect_string(__wrap__merror, formatted_msg, "(6611): 'realtime_adddir' failed, the directory 'c:\\windows\\system32\\drivers\\etc' couldn't be added to real time mode.");
    expect_string(__wrap__mdebug1, formatted_msg, "(6225): The 'c:\\windows\\system32\\wbem' directory starts to be monitored in real-time mode.");

    set_whodata_mode_changes();
}

void test_fim_whodata_initialize_eventchannel(void **state) {
    int ret;
    int i;
    char *dirs[] = {
        "%WINDIR%\\System32\\WindowsPowerShell\\v1.0",
        NULL
    };
    char expanded_dirs[1][OS_SIZE_1024];

    // Expand directories
    for(i = 0; dirs[i]; i++) {
        if(!ExpandEnvironmentStrings(dirs[i], expanded_dirs[i], OS_SIZE_1024))
            fail();

        str_lowercase(expanded_dirs[i]);
        expect_realtime_adddir_call(expanded_dirs[i], 0);
    }

    will_return(__wrap_run_whodata_scan, 0);

    will_return(wrap_CreateThread, (HANDLE)123456);

    ret = fim_whodata_initialize();

    assert_int_equal(ret, 0);
}
#endif  // WIN_WHODATA
#endif

void test_fim_send_sync_msg_10_eps(void ** state) {
    (void) state;
    syscheck.sync_max_eps = 10;
    char location[10] = "fim_file";

    // We must not sleep the first 9 times

    for (int i = 1; i < syscheck.sync_max_eps; i++) {
        expect_string(__wrap__mdebug2, formatted_msg, "(6317): Sending integrity control message: ");
        expect_w_send_sync_msg("", location, DBSYNC_MQ, 0);
        fim_send_sync_msg( location, "");
    }

#ifndef TEST_WINAGENT
    expect_value(__wrap_sleep, seconds, 1);
#else
    expect_value(wrap_Sleep, dwMilliseconds, 1000);
#endif

    // After 10 times, sleep one second
    expect_string(__wrap__mdebug2, formatted_msg, "(6317): Sending integrity control message: ");
    expect_w_send_sync_msg("", location, DBSYNC_MQ, 0);
    fim_send_sync_msg( location, "");
}

void test_fim_send_sync_msg_0_eps(void ** state) {
    (void) state;
    syscheck.sync_max_eps = 0;
    char location[10] = "fim_file";
    // We must not sleep
    expect_string(__wrap__mdebug2, formatted_msg, "(6317): Sending integrity control message: ");
    expect_w_send_sync_msg("", location, DBSYNC_MQ, 0);
    fim_send_sync_msg(location, "");
}

void test_send_syscheck_msg_10_eps(void ** state) {
    syscheck.max_eps = 10;
    cJSON *event = cJSON_CreateObject();

    if (event == NULL) {
        fail_msg("Failed to create cJSON object");
    }

    // We must not sleep the first 9 times

    for (int i = 1; i < syscheck.max_eps; i++) {
        expect_string(__wrap__mdebug2, formatted_msg, "(6321): Sending FIM event: {}");
        expect_w_send_sync_msg("{}", SYSCHECK, SYSCHECK_MQ, 0);
        send_syscheck_msg(event);
    }

#ifndef TEST_WINAGENT
    expect_value(__wrap_sleep, seconds, 1);
#else
    expect_value(wrap_Sleep, dwMilliseconds, 1000);
#endif

    // After 10 times, sleep one second
    expect_string(__wrap__mdebug2, formatted_msg, "(6321): Sending FIM event: {}");
    expect_w_send_sync_msg("{}", SYSCHECK, SYSCHECK_MQ, 0);

    send_syscheck_msg(event);

    cJSON_Delete(event);
}

void test_send_syscheck_msg_0_eps(void ** state) {
    syscheck.max_eps = 0;
    cJSON *event = cJSON_CreateObject();

    if (event == NULL) {
        fail_msg("Failed to create cJSON object");
    }

    // We must not sleep
    expect_string(__wrap__mdebug2, formatted_msg, "(6321): Sending FIM event: {}");
    expect_w_send_sync_msg("{}", SYSCHECK, SYSCHECK_MQ, 0);
    send_syscheck_msg(event);
}

void test_fim_send_scan_info(void **state) {
    (void) state;
    const char *msg = "{\"type\":\"scan_start\",\"data\":{\"timestamp\":1}}";
#ifndef TEST_WINAGENT
    will_return(__wrap_time, 1);
#endif
    expect_string(__wrap__mdebug2, formatted_msg, "(6321): Sending FIM event: {\"type\":\"scan_start\",\"data\":{\"timestamp\":1}}");
    expect_w_send_sync_msg(msg, SYSCHECK, SYSCHECK_MQ, 0);
    fim_send_scan_info(FIM_SCAN_START);
}

#ifndef TEST_WINAGENT
void test_fim_link_update(void **state) {
    char *new_path = "/new_path";

    expect_fim_db_get_path_from_pattern(syscheck.database, "/folder/%", NULL, FIM_DB_DISK, FIMDB_OK);
    expect_string(__wrap_remove_audit_rule_syscheck, path, syscheck.directories[1]->symbolic_links);

    expect_realtime_adddir_call(new_path, 0);
    expect_fim_checker_call(new_path, syscheck.directories[1]);

    fim_link_update(new_path, syscheck.directories[1]);

    assert_string_equal(syscheck.directories[1]->path, "/link");
    assert_string_equal(syscheck.directories[1]->symbolic_links, new_path);
}

void test_fim_link_update_already_added(void **state) {
    char *link_path = "/home";
    char error_msg[OS_SIZE_128];

    free(syscheck.directories[1]->symbolic_links);
    syscheck.directories[1]->symbolic_links = strdup("/home");

    snprintf(error_msg, OS_SIZE_128, FIM_LINK_ALREADY_ADDED, link_path);

    expect_string(__wrap__mdebug1, formatted_msg, error_msg);

    fim_link_update(link_path, syscheck.directories[1]);

    assert_string_equal(syscheck.directories[1]->path, "/link");
    assert_null(syscheck.directories[1]->symbolic_links);
}

void test_fim_link_check_delete(void **state) {
    char *link_path = "/link";
    char *pointed_folder = "/folder";

    expect_string(__wrap_lstat, filename, syscheck.directories[1]->symbolic_links);
    will_return(__wrap_lstat, 0);
    will_return(__wrap_lstat, 0);

    expect_fim_db_get_path_from_pattern(syscheck.database, "/folder/%", NULL, FIM_DB_DISK, FIMDB_OK);

    expect_string(__wrap_remove_audit_rule_syscheck, path, syscheck.directories[1]->symbolic_links);

    expect_fim_configuration_directory_call(pointed_folder, NULL);
    fim_link_check_delete(syscheck.directories[1]);

    assert_string_equal(syscheck.directories[1]->path, link_path);
    assert_null(syscheck.directories[1]->symbolic_links);
}

void test_fim_link_check_delete_lstat_error(void **state) {
    char *link_path = "/link";
    char *pointed_folder = "/folder";
    char error_msg[OS_SIZE_128];

    expect_string(__wrap_lstat, filename, pointed_folder);
    will_return(__wrap_lstat, 0);
    will_return(__wrap_lstat, -1);
    errno = 0;

    snprintf(error_msg, OS_SIZE_128, FIM_STAT_FAILED, pointed_folder, 0, "Success");

    expect_string(__wrap__mdebug1, formatted_msg, error_msg);

    fim_link_check_delete(syscheck.directories[1]);

    assert_string_equal(syscheck.directories[1]->path, link_path);
    assert_string_equal(syscheck.directories[1]->symbolic_links, pointed_folder);
}

void test_fim_link_check_delete_noentry_error(void **state) {
    char *link_path = "/link";
    char *pointed_folder = "/folder";

    expect_string(__wrap_lstat, filename, pointed_folder);
    will_return(__wrap_lstat, 0);
    will_return(__wrap_lstat, -1);
    expect_string(__wrap_remove_audit_rule_syscheck, path, syscheck.directories[1]->symbolic_links);

    errno = ENOENT;

    fim_link_check_delete(syscheck.directories[1]);

    errno = 0;

    assert_string_equal(syscheck.directories[1]->path, link_path);
    assert_null(syscheck.directories[1]->symbolic_links);
}

void test_fim_delete_realtime_watches(void **state) {
    (void) state;

    unsigned int pos;
    char *link_path = "/link";
    char *pointed_folder = "/folder";

    expect_fim_configuration_directory_call(pointed_folder, syscheck.directories[0]);
    expect_fim_configuration_directory_call("data", syscheck.directories[0]);

    will_return(__wrap_inotify_rm_watch, 1);

    fim_delete_realtime_watches(syscheck.directories[1]);

    assert_null(OSHash_Begin(syscheck.realtime->dirtb, &pos));
}

void test_fim_link_delete_range(void **state) {
    fim_tmp_file *tmp_file = *state;

    expect_fim_db_get_path_from_pattern(syscheck.database, "/folder/%", tmp_file, FIM_DB_DISK, FIMDB_OK);
    expect_wrapper_fim_db_delete_range_call(syscheck.database, FIM_DB_DISK, tmp_file, FIMDB_OK);
    fim_link_delete_range(syscheck.directories[1]);
}

void test_fim_link_delete_range_error(void **state) {
    char error_msg[OS_SIZE_128];
    fim_tmp_file *tmp_file = *state;

    snprintf(error_msg, OS_SIZE_128, FIM_DB_ERROR_RM_PATTERN, "/folder/%");
    expect_fim_db_get_path_from_pattern(syscheck.database, "/folder/%", tmp_file, FIM_DB_DISK, FIMDB_OK);

    expect_wrapper_fim_db_delete_range_call(syscheck.database, FIM_DB_DISK, tmp_file, FIMDB_ERR);
    expect_string(__wrap__merror, formatted_msg, error_msg);

    fim_link_delete_range(syscheck.directories[1]);
}

void test_fim_link_silent_scan(void **state) {
    char *link_path = "/link";

    expect_realtime_adddir_call(link_path, 0);
    expect_fim_checker_call(link_path, syscheck.directories[3]);

    fim_link_silent_scan(link_path, syscheck.directories[3]);
}

void test_fim_link_reload_broken_link_already_monitored(void **state) {
    char *link_path = "/link";
    char *pointed_folder = "/folder";
    char error_msg[OS_SIZE_128];

    snprintf(error_msg, OS_SIZE_128, FIM_LINK_ALREADY_ADDED, link_path);

    expect_string(__wrap__mdebug1, formatted_msg, error_msg);

    fim_link_reload_broken_link(link_path, syscheck.directories[1]);

    assert_string_equal(syscheck.directories[1]->path, link_path);
    assert_string_equal(syscheck.directories[1]->symbolic_links, pointed_folder);
}

void test_fim_link_reload_broken_link_reload_broken(void **state) {
    char *link_path = "/link";
    char *pointed_folder = "/new_path";

    expect_fim_checker_call(pointed_folder, syscheck.directories[1]);

    expect_string(__wrap_realtime_adddir, dir, pointed_folder);
    will_return(__wrap_realtime_adddir, 0);

    fim_link_reload_broken_link(pointed_folder, syscheck.directories[1]);

    assert_string_equal(syscheck.directories[1]->path, link_path);
    assert_string_equal(syscheck.directories[1]->symbolic_links, pointed_folder);
}
#endif

void test_check_max_fps_no_sleep(void **state) {
    will_return(__wrap_gettime, last_time + 1);
    check_max_fps();
}

void test_check_max_fps_sleep(void **state) {
    last_time = 10;
    files_read = syscheck.max_files_per_second;

    will_return(__wrap_gettime, last_time);
    expect_string(__wrap__mdebug2, formatted_msg, FIM_REACHED_MAX_FPS);
    check_max_fps();
}

int main(void) {
#ifndef WIN_WHODATA
    const struct CMUnitTest tests[] = {
#ifdef TEST_WINAGENT
        cmocka_unit_test(test_set_priority_windows_thread_highest),
        cmocka_unit_test(test_set_priority_windows_thread_above_normal),
        cmocka_unit_test(test_set_priority_windows_thread_normal),
        cmocka_unit_test(test_set_priority_windows_thread_below_normal),
        cmocka_unit_test(test_set_priority_windows_thread_lowest),
        cmocka_unit_test(test_set_priority_windows_thread_idle),
        cmocka_unit_test(test_set_priority_windows_thread_error),
#endif

        cmocka_unit_test(test_log_realtime_status),
        cmocka_unit_test(test_fim_send_msg),
        cmocka_unit_test(test_fim_send_msg_retry),
        cmocka_unit_test(test_fim_send_msg_retry_error),
        cmocka_unit_test(test_fim_send_sync_msg_10_eps),
        cmocka_unit_test(test_fim_send_sync_msg_0_eps),
        cmocka_unit_test(test_send_syscheck_msg_10_eps),
        cmocka_unit_test(test_send_syscheck_msg_0_eps),
        cmocka_unit_test(test_fim_send_scan_info),
        cmocka_unit_test_setup_teardown(test_check_max_fps_no_sleep, setup_max_fps, teardown_max_fps),
        cmocka_unit_test_setup_teardown(test_check_max_fps_sleep, setup_max_fps, teardown_max_fps),
#ifndef TEST_WINAGENT
        cmocka_unit_test_setup_teardown(test_fim_link_update, setup_symbolic_links, teardown_symbolic_links),
        cmocka_unit_test_setup_teardown(test_fim_link_update_already_added, setup_symbolic_links, teardown_symbolic_links),
        cmocka_unit_test_setup_teardown(test_fim_link_check_delete, setup_symbolic_links, teardown_symbolic_links),
        cmocka_unit_test_setup_teardown(test_fim_link_check_delete_lstat_error, setup_symbolic_links, teardown_symbolic_links),
        cmocka_unit_test_setup_teardown(test_fim_link_check_delete_noentry_error, setup_symbolic_links, teardown_symbolic_links),
        cmocka_unit_test_setup_teardown(test_fim_delete_realtime_watches, setup_symbolic_links, teardown_symbolic_links),
        cmocka_unit_test_setup_teardown(test_fim_link_delete_range, setup_tmp_file, teardown_tmp_file),
        cmocka_unit_test_setup_teardown(test_fim_link_delete_range_error, setup_tmp_file, teardown_tmp_file),
        cmocka_unit_test_setup_teardown(test_fim_link_silent_scan, setup_symbolic_links, teardown_symbolic_links),
        cmocka_unit_test_setup_teardown(test_fim_link_reload_broken_link_already_monitored, setup_symbolic_links, teardown_symbolic_links),
        cmocka_unit_test_setup_teardown(test_fim_link_reload_broken_link_reload_broken, setup_symbolic_links, teardown_symbolic_links),
#endif
    };

    return cmocka_run_group_tests(tests, setup_group, teardown_group);
#else  // WIN_WHODATA
    const struct CMUnitTest eventchannel_tests[] = {
        cmocka_unit_test(test_fim_whodata_initialize),
        cmocka_unit_test(test_set_whodata_mode_changes),
        cmocka_unit_test(test_fim_whodata_initialize_eventchannel),
        cmocka_unit_test(test_fim_whodata_initialize_fail_set_policies),
    };
    return cmocka_run_group_tests(eventchannel_tests, setup_group, teardown_group);
#endif
}
