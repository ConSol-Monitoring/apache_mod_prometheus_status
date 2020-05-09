/*
**  mod_prometheus_status.c -- Apache sample prometheus_status module
*/

#include "mod_prometheus_status.h"
#include "mod_prometheus_status_go.h"

extern apr_hash_t *log_hash;
extern unixd_config_rec ap_unixd_config;

#define SERVER_DISABLED SERVER_NUM_STATUS
#define MOD_STATUS_NUM_STATUS (SERVER_NUM_STATUS+1)
static int status_flags[MOD_STATUS_NUM_STATUS];
static int server_limit, thread_limit, threads_per_child, max_servers;
static apr_proc_t *g_metric_manager = NULL;
static int g_metric_manager_keep_running = TRUE;

typedef struct {
    char                context[4096];
    /* server level options */
    int                 debug;              /* Enable debug logging */
    const char         *label_names;        /* Set custom label names */
    const char         *time_buckets;       /* raw response time buckets */
    const char         *size_buckets;       /* raw response size buckets */
    const char         *tmp_folder;         /* tmp folder for the socket */

    /* directory level options */
    int                 enabled;            /* Enable or disable our module */
    char                label_values[4096]; /* Add custom label values */
    apr_array_header_t *label_format;       /* parsed label format */
} prometheus_status_config;
static prometheus_status_config config;

/* Server object for main server as supplied to prometheus_status_init(). */
static server_rec *main_server = NULL;

int (*prometheusStatusInitFn)() = NULL;
char *metric_socket = NULL;
int metric_socket_fd = 0;

/* Handler for the "PrometheusStatusDebug" directive */
const char *prometheus_status_set_debug(cmd_parms *cmd, void *cfg, int val) {
    config.debug = val;
    return NULL;
}

/* Handler for the "PrometheusStatusEnabled" directive */
const char *prometheus_status_set_enabled(cmd_parms *cmd, void *cfg, int val) {
    prometheus_status_config *conf = (prometheus_status_config *) cfg;
    conf->enabled = val;
    return NULL;
}

/* Handler for the "PrometheusStatusLabelNames" directive */
static const char *prometheus_status_set_label_names(cmd_parms *cmd, void *cfg, const char *arg) {
    config.label_names  = arg;
    return NULL;
}

/* Handler for the "PrometheusStatusTmpFolder" directive */
static const char *prometheus_status_set_tmp_folder(cmd_parms *cmd, void *cfg, const char *arg) {
    config.tmp_folder  = arg;
    return NULL;
}

/* Handler for the "PrometheusStatusResponseTimeBuckets" directive */
static const char *prometheus_status_set_time_buckets(cmd_parms *cmd, void *cfg, const char *arg) {
    config.size_buckets  = arg;
    return NULL;
}

/* Handler for the "PrometheusStatusResponseSizeBuckets" directive */
static const char *prometheus_status_set_size_buckets(cmd_parms *cmd, void *cfg, const char *arg) {
    config.size_buckets  = arg;
    return NULL;
}

/* Handler for the "PrometheusStatusLabelValues" directive */
const char *prometheus_status_set_label_values(cmd_parms *cmd, void *cfg, const char *arg) {
    const char *err_string = NULL;
    prometheus_status_config *conf = (prometheus_status_config *) cfg;
    strcpy(conf->label_values, arg);
    conf->label_format = parse_log_string(cmd->pool, conf->label_values, &err_string);
    return err_string;
}

/* open the communication socket */
static int prometheus_status_open_communication_socket() {
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    // reuse if already open
    if(metric_socket_fd != 0) {
        return(TRUE);
    }
    strcpy(addr.sun_path, metric_socket);
    metric_socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if(connect(metric_socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        logErrorf("failed to open metrics socket: socket:%s fd:%d errno:%d (%s)", metric_socket, metric_socket_fd, errno, strerror(errno));
        return(FALSE);
    }

    struct timeval timeout;
    timeout.tv_sec  = DEFAULTSOCKETTIMEOUT;
    timeout.tv_usec = 0;

    if(setsockopt(metric_socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) == -1) {
        logErrorf("setsockopt failed: socket:%s fd:%d errno:%d (%s)", metric_socket, metric_socket_fd, errno, strerror(errno));
        return(FALSE);
    }

    if(setsockopt(metric_socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) == -1) {
        logErrorf("setsockopt failed: socket:%s fd:%d errno:%d (%s)", metric_socket, metric_socket_fd, errno, strerror(errno));
        return(FALSE);
    }

    return(TRUE);
}

/* close the communication socket */
static int prometheus_status_close_communication_socket() {
    if(metric_socket_fd == 0) {
        return(TRUE);
    }
    close(metric_socket_fd);
    metric_socket_fd = 0;
    return(TRUE);
}

/* send something over the communication socket */
static int prometheus_status_send_communication_socket(const char *fmt, ...) {
    char buffer[4096];
    int nbytes;
    va_list ap;

    // open socket unless open
    if(metric_socket_fd == 0) {
        if(!prometheus_status_open_communication_socket()) {
            return(FALSE);
        }
    }

    va_start(ap, fmt);
    nbytes = vsnprintf(buffer, 4096, fmt, ap);
    va_end(ap);

    if(write(metric_socket_fd, buffer, nbytes) < 0) {
        logErrorf("failed to send to metrics collector: socket:%s fd:%d errno:%d (%s)", metric_socket, metric_socket_fd, errno, strerror(errno));
        prometheus_status_close_communication_socket();
    }
    return(TRUE);
}

/* gather non-request runtime metrics */
static int prometheus_status_monitor() {
    int busy = 0;
    int ready = 0;
    int i, j, res;
    worker_score *ws_record;
    process_score *ps_record;
    ap_generation_t mpm_generation;
    apr_interval_time_t uptime;
    apr_time_t nowtime;
    ap_loadavg_t cpu;

    status_flags[SERVER_DEAD]           = 0;
    status_flags[SERVER_READY]          = 0;
    status_flags[SERVER_STARTING]       = 0;
    status_flags[SERVER_BUSY_READ]      = 0;
    status_flags[SERVER_BUSY_WRITE]     = 0;
    status_flags[SERVER_BUSY_KEEPALIVE] = 0;
    status_flags[SERVER_BUSY_LOG]       = 0;
    status_flags[SERVER_BUSY_DNS]       = 0;
    status_flags[SERVER_CLOSING]        = 0;
    status_flags[SERVER_GRACEFUL]       = 0;
    status_flags[SERVER_IDLE_KILL]      = 0;
    status_flags[SERVER_DISABLED]       = 0;

    ap_mpm_query(AP_MPMQ_GENERATION, &mpm_generation);

    nowtime = apr_time_now();
    uptime = (apr_uint32_t) apr_time_sec(nowtime - ap_scoreboard_image->global->restart_time);
    prometheus_status_send_communication_socket("server:promServerUptime;%ld\n", uptime);

    prometheus_status_send_communication_socket("server:promMPMGeneration;%d\n", mpm_generation);
    prometheus_status_send_communication_socket("server:promConfigGeneration;%d\n", ap_state_query(AP_SQ_CONFIG_GEN));

    ap_get_loadavg(&cpu);
    prometheus_status_send_communication_socket("server:promCPULoad;%f\n", cpu.loadavg);

    for(i = 0; i < server_limit; ++i) {
        ps_record = ap_get_scoreboard_process(i);
        for(j = 0; j < thread_limit; ++j) {
            ws_record = ap_get_scoreboard_worker_from_indexes(i, j);
            res = ws_record->status;

            if ((i >= max_servers || j >= threads_per_child)
                && (res == SERVER_DEAD))
                status_flags[SERVER_DISABLED]++;
            else
                status_flags[res]++;

            if(!ps_record->quiescing && ps_record->pid) {
                if(res == SERVER_READY) {
                    if(ps_record->generation == mpm_generation)
                        ready++;
                }
                else if (res != SERVER_DEAD &&
                         res != SERVER_STARTING &&
                         res != SERVER_IDLE_KILL) {
                    busy++;
                }
            }
        }
    }

    prometheus_status_send_communication_socket("server:promScoreboard;%d;idle\n",          status_flags[SERVER_READY]);
    prometheus_status_send_communication_socket("server:promScoreboard;%d;startup\n",       status_flags[SERVER_STARTING]);
    prometheus_status_send_communication_socket("server:promScoreboard;%d;read\n",          status_flags[SERVER_BUSY_READ]);
    prometheus_status_send_communication_socket("server:promScoreboard;%d;reply\n",         status_flags[SERVER_BUSY_WRITE]);
    prometheus_status_send_communication_socket("server:promScoreboard;%d;keepalive\n",     status_flags[SERVER_BUSY_KEEPALIVE]);
    prometheus_status_send_communication_socket("server:promScoreboard;%d;logging\n",       status_flags[SERVER_BUSY_LOG]);
    prometheus_status_send_communication_socket("server:promScoreboard;%d;closing\n",       status_flags[SERVER_CLOSING]);
    prometheus_status_send_communication_socket("server:promScoreboard;%d;graceful_stop\n", status_flags[SERVER_GRACEFUL]);
    prometheus_status_send_communication_socket("server:promScoreboard;%d;idle_cleanup\n",  status_flags[SERVER_IDLE_KILL]);
    prometheus_status_send_communication_socket("server:promScoreboard;%d;disabled\n",      status_flags[SERVER_DISABLED]);

    prometheus_status_send_communication_socket("server:promWorkers;%d;ready\n", ready);
    prometheus_status_send_communication_socket("server:promWorkers;%d;busy\n", busy);

    return OK;
}

/* prometheus_status_handler responds to /metrics requests */
static int prometheus_status_handler(request_rec *r) {
    int nbytes;
    char buffer[32768];

    // is the module enabled at all?
    prometheus_status_config *config = (prometheus_status_config*) ap_get_module_config(r->server->module_config, &prometheus_status_module);
    if(config->enabled == 0) {
        return(OK);
    }

    if(!r->handler || strcmp(r->handler, "prometheus-metrics")) return(DECLINED);
    if(r->header_only) {
        return(OK);
    }

    // update runtime metrics
    prometheus_status_monitor();

    ap_set_content_type(r, "text/plain");

    if(!prometheus_status_send_communication_socket("metrics\n")) {
        ap_rputs("ERROR: failed fetch metrics\n", r);
        return(HTTP_INTERNAL_SERVER_ERROR);
    }

    while((nbytes = read(metric_socket_fd, buffer, 32768)) > 0) {
        if(nbytes < 0) {
            logErrorf("reading metrics failed: socket:%s fd:%d errno:%d (%s)", metric_socket, metric_socket_fd, errno, strerror(errno));
            ap_rputs("ERROR: failed fetch metrics\n", r);
            return(HTTP_INTERNAL_SERVER_ERROR);
        }
        if(nbytes == 0) {
            break;
        }
        buffer[nbytes] = 0;
        ap_rputs(buffer, r);
        // double newline at the end means EOF
        if(nbytes > 3 && buffer[nbytes-1] == '\n' && buffer[nbytes-2] == '\n') {
            break;
        }
    }

    prometheus_status_close_communication_socket();

    return(OK);
}

/* prometheus_status_counter is called on each request to update counter */
static int prometheus_status_counter(request_rec *r) {
    apr_time_t now = apr_time_now();
    apr_time_t duration = now - r->request_time;

    // is the module enabled at all?
    prometheus_status_config *cfg = (prometheus_status_config*) ap_get_module_config(r->per_dir_config, &prometheus_status_module);
    if(cfg->enabled == 0) {
        return(OK);
    }

    const char *label = NULL;
    apr_array_header_t *format = cfg->label_format != NULL ? cfg->label_format : config.label_format;
    prometheus_status_expand_variables(format, r, &label);

    prometheus_status_send_communication_socket("request:promRequests;1;%s\n", label);
    prometheus_status_send_communication_socket("request:promResponseTime;%f;%s\n", (long)duration/(double)APR_USEC_PER_SEC, label);
    prometheus_status_send_communication_socket("request:promResponseSize;%d;%s\n", (int)r->bytes_sent, label);
    prometheus_status_close_communication_socket();
    return(OK);
}

static apr_status_t prometheus_status_cleanup_handler() {
    if(metric_socket != NULL) {
        logDebugf("prometheus_status_cleanup_handler removing %s", metric_socket);
        unlink(metric_socket);
        free(metric_socket);
        metric_socket = NULL;
    }
    g_metric_manager_keep_running = FALSE;
    return(OK);
}

/* prometheus_status_load_gomodule loads/starts the go part */
static void prometheus_status_load_gomodule(apr_pool_t *p, server_rec *s) {
    const char* mpm_name = ap_show_mpm();

    // detect go module .so location
    void *go_module_handle = NULL;
    char origin[PATH_MAX];
    apr_os_dso_handle_t *osdso;
    apr_os_dso_handle_get((void *)&osdso, prometheus_status_module.dynamic_load_handle);
    if(dlinfo(osdso, RTLD_DI_ORIGIN, &origin) != -1) {
        char go_so_path[PATH_MAX+100];
        snprintf(go_so_path, PATH_MAX+100, "%s/mod_prometheus_status_go.so", origin);

        go_module_handle = dlopen(go_so_path, RTLD_LAZY);
        if(!go_module_handle) {
            logErrorf("loading %s failed: %s\n", go_so_path, dlerror());
            exit(1);
        }
    }
    logDebugf("prometheus_status_init gomodule loaded");

    prometheusStatusInitFn = dlsym(go_module_handle, "prometheusStatusInit");

    // run go initializer
    int rc = (*prometheusStatusInitFn)(
        metric_socket,
        ap_get_server_description(),
        s->server_hostname,
        VERSION,
        config.debug,
        ap_unixd_config.user_id,
        ap_unixd_config.group_id,
        config.label_names,
        mpm_name,
        DEFAULTSOCKETTIMEOUT,
        config.time_buckets,
        config.size_buckets
    );
    if(rc != 0) {
        logErrorf("mod_prometheus_status initializing failed");
        exit(1);
    }

    return;
}

static void prometheus_status_metric_manager_maint(int reason, void *data, apr_wait_t status) {
    logDebugf("prometheus_status_metric_manager_maint: %d", reason);
    apr_proc_t *proc = data;

    switch (reason) {
        case APR_OC_REASON_DEATH:
        case APR_OC_REASON_RESTART:
        case APR_OC_REASON_LOST:
            prometheus_status_cleanup_handler();
            apr_proc_other_child_unregister(data);
            break;
        case APR_OC_REASON_UNREGISTER:
            prometheus_status_cleanup_handler();
            kill(proc->pid, SIGHUP);
            break;
    }
 }

static apr_status_t prometheus_status_create_metrics_manager(apr_pool_t * p, server_rec * s) {
    apr_status_t rv;

    g_metric_manager = (apr_proc_t *) apr_pcalloc(p, sizeof(*g_metric_manager));
    rv = apr_proc_fork(g_metric_manager, p);
    if(rv == APR_INCHILD) {
        // child process
        // if running as root, switch to configured user
        if(ap_unixd_config.suexec_enabled) {
            if(getuid() != 0) {
                logErrorf("current user is not root while suexec is enabled, exiting now");
                exit(1);
            }
            if(setgid(ap_unixd_config.group_id) == -1) {
                logErrorf("setgid: unable to set group id to Group %u", (unsigned) ap_unixd_config.group_id);
                return -1;
            }
            /* Only try to switch if we're running as root */
            if(!geteuid() && (seteuid(ap_unixd_config.user_id) == -1)) {
                logErrorf("seteuid: unable to change to uid %ld", (long) ap_unixd_config.user_id);
                exit(1);
            }
        } else
            ap_unixd_setup_child();

        // load all go stuff in a separated sub process
        prometheus_status_load_gomodule(p, s);
        // wait till process ends...
        while(g_metric_manager_keep_running) {
            sleep(60);
        }

        logDebugf("metrics manager exited");
        exit(0);
    } else if (rv != APR_INPARENT) {
        logErrorf("cannot create metrics manager");
        exit(1);
    }

    // parent process
    apr_pool_note_subprocess(p, g_metric_manager, APR_KILL_ONLY_ONCE);
    apr_proc_other_child_register(g_metric_manager, prometheus_status_metric_manager_maint, g_metric_manager, NULL, p);

    // wait till socket exists
    struct stat buffer;
    int retries = 0;
    while(stat(metric_socket, &buffer) != 0) {
        usleep(50000);
        retries++;
        if(retries > 20) {
            logErrorf("metrics manager failed to start in time");
            break;
        }
    }

    return APR_SUCCESS;
}

/* prometheus_status_init registers and initializes all counter */
static int prometheus_status_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
    void *data = NULL;
    const char *key = "prometheus_status_init";

    // This code is used to prevent double initialization of the module during Apache startup
    apr_pool_userdata_get(&data, key, s->process->pool);
    if(data == NULL) {
        apr_pool_userdata_set((const void *)1, key, apr_pool_cleanup_null, s->process->pool);
        return OK;
    }

    /* cache main server */
    main_server = s;

    ap_mpm_query(AP_MPMQ_HARD_LIMIT_THREADS, &thread_limit);
    ap_mpm_query(AP_MPMQ_HARD_LIMIT_DAEMONS, &server_limit);
    ap_mpm_query(AP_MPMQ_MAX_DAEMONS, &max_servers);
    ap_mpm_query(AP_MPMQ_MAX_THREADS, &threads_per_child);

    /* work around buggy MPMs */
    if (threads_per_child == 0)
        threads_per_child = 1;

    prometheus_status_cleanup_handler();
    g_metric_manager_keep_running = TRUE;
    metric_socket = tempnam(config.tmp_folder, "mtr.");
    logDebugf("prometheus_status_init: version %s - using tmp socket %s", VERSION, metric_socket);

    prometheus_status_create_metrics_manager(p, s);

    return OK;
}

/* prometheus_status_register_hooks registers all required hooks */
static void prometheus_status_register_hooks(apr_pool_t *p) {
    logDebugf("prometheus_status_register_hooks: %s\n", __FILE__);
    const char *err_string = NULL;
    // set defaults
    config.debug        = DEFAULTDEBUG;
    config.label_names  = DEFAULTLABELNAMES;
    config.time_buckets = DEFAULTTIMEBUCKETS;
    config.size_buckets = DEFAULTSIZEBUCKETS;
    config.tmp_folder   = DEFAULTTMPFOLDER;
    strcpy(config.label_values, DEFAULTLABELVALUES);

    log_hash = apr_hash_make(p);
    prometheus_status_register_all_log_handler(p);
    config.label_format = parse_log_string(p, config.label_values, &err_string);
    if(err_string != NULL) {
        logErrorf("failed to parse label values: %s\n", err_string);
        exit(1);
    }

    ap_hook_handler(prometheus_status_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_post_config(prometheus_status_init, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_log_transaction(prometheus_status_counter, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Function for creating new configurations for per-directory contexts */
void *prometheus_status_create_dir_conf(apr_pool_t *pool, char *context) {
    context = context ? context : "Newly created configuration";

    prometheus_status_config *cfg = apr_pcalloc(pool, sizeof(prometheus_status_config));
    if(cfg) {
        // Set some default values
        strcpy(cfg->context, context);
        strcpy(cfg->label_values, "");
        cfg->enabled = -1;
    }
    return cfg;
}

/* Merging function for configurations */
void *prometheus_status_merge_dir_conf(apr_pool_t *pool, void *BASE, void *ADD) {
    prometheus_status_config *base = (prometheus_status_config *) BASE;
    prometheus_status_config *add  = (prometheus_status_config *) ADD;
    prometheus_status_config *conf = (prometheus_status_config *) prometheus_status_create_dir_conf(pool, "Merged configuration");

    conf->enabled = (add->enabled != -1) ? add->enabled : base->enabled;
    strcpy(conf->label_values, strlen(add->label_values) ? add->label_values : base->label_values);
    conf->label_format = add->label_format != NULL ? add->label_format : base->label_format;
    return conf;
}

/* Function for creating new configurations for per-server contexts */
void *prometheus_status_create_server_conf(apr_pool_t *pool, server_rec *s) {
    return(prometheus_status_create_dir_conf(pool, "server config"));
}

/* available configuration directives */
static const command_rec prometheus_status_directives[] = {
    /* server level */
    AP_INIT_FLAG("PrometheusStatusDebug",                   prometheus_status_set_debug,         NULL, RSRC_CONF, "Set to On to debug output."),
    AP_INIT_RAW_ARGS("PrometheusStatusLabelNames",          prometheus_status_set_label_names,   NULL, RSRC_CONF, "Set a request specific label names from within apache directives."),
    AP_INIT_RAW_ARGS("PrometheusStatusTmpFolder",           prometheus_status_set_tmp_folder,    NULL, RSRC_CONF, "Set folder for communication socket."),
    AP_INIT_RAW_ARGS("PrometheusStatusResponseTimeBuckets", prometheus_status_set_time_buckets,  NULL, RSRC_CONF, "Set response time histogram buckets."),
    AP_INIT_RAW_ARGS("PrometheusStatusResponseSizeBuckets", prometheus_status_set_size_buckets,  NULL, RSRC_CONF, "Set response size histogram buckets."),

    /* directory level */
    AP_INIT_FLAG("PrometheusStatusEnabled",                 prometheus_status_set_enabled,       NULL, OR_ALL,    "Set to Off to disable collecting metrics (for this directory/location)."),
    AP_INIT_RAW_ARGS("PrometheusStatusLabelValues",         prometheus_status_set_label_values,  NULL, OR_ALL,    "Set a request label values from within apache directives"),
    { NULL }
};

/* register mod_prometheus_status within the apache */
module AP_MODULE_DECLARE_DATA prometheus_status_module = {
    STANDARD20_MODULE_STUFF,
    prometheus_status_create_dir_conf,      /* create per-dir    config structures */
    prometheus_status_merge_dir_conf,       /* merge  per-dir    config structures */
    prometheus_status_create_server_conf,   /* create per-server config structures */
    prometheus_status_merge_dir_conf,       /* merge  per-server config structures */
    prometheus_status_directives,           /* table of config file commands       */
    prometheus_status_register_hooks        /* register hooks                      */
};
