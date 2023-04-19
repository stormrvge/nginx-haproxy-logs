#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_file.h>
#include <ngx_files.h>

typedef struct {
    ngx_flag_t enable;
    ngx_str_t output_file;
} ngx_http_performance_metrics_loc_conf_t;

static ngx_int_t ngx_http_performance_metrics_init(ngx_conf_t *cf);
static char *ngx_http_performance_metrics_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static void *ngx_http_performance_metrics_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_log_performance_metrics(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_performance_metrics_log_handler(ngx_http_request_t *r);
static void log_performance_metrics(ngx_http_request_t *r);

static ngx_command_t ngx_http_performance_metrics_commands[] = {
    { ngx_string("log_performance_metrics"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_log_performance_metrics,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_performance_metrics_loc_conf_t, enable),
      NULL },

    ngx_null_command
};

static ngx_http_module_t ngx_http_performance_metrics_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_http_performance_metrics_init,      /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_http_performance_metrics_create_loc_conf,  /* create location configuration */
    ngx_http_performance_metrics_merge_loc_conf,   /* merge location configuration */
};

ngx_module_t ngx_http_performance_metrics_module = {
    NGX_MODULE_V1,
    &ngx_http_performance_metrics_module_ctx,
    ngx_http_performance_metrics_commands,
    NGX_HTTP_MODULE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NGX_MODULE_V1_PADDING
};

static ngx_int_t ngx_http_performance_metrics_init(ngx_conf_t *cf) {
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_performance_metrics_log_handler;

    return NGX_OK;
}

static void *ngx_http_performance_metrics_create_loc_conf(ngx_conf_t *cf) {
    ngx_http_performance_metrics_loc_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_performance_metrics_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable = NGX_CONF_UNSET;
    return conf;
}

static char *ngx_http_performance_metrics_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
    ngx_http_performance_metrics_loc_conf_t *prev = parent;
    ngx_http_performance_metrics_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_str_value(conf->output_file, prev->output_file, "");

    return NGX_CONF_OK;
}

static char *ngx_http_log_performance_metrics(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_performance_metrics_loc_conf_t *pmcf = conf;
    ngx_str_t *value;

    value = cf->args->elts;
    if (ngx_conf_full_name(cf->cycle, &value[1], 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
    pmcf->output_file = value[1];
    pmcf->enable = 1;

    // Log the output file path using ngx_log_error
    ngx_log_error(NGX_LOG_DEBUG, cf->cycle->log, 0, "Output file path: %V", &pmcf->output_file);

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_performance_metrics_log_handler(ngx_http_request_t *r) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "[PERFORMANCE_METRICS] before log_performance_metrics function");
    log_performance_metrics(r);
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "[PERFORMANCE_METRICS] after log_performance_metrics function");
    return NGX_OK;
}

static void log_performance_metrics(ngx_http_request_t *r) {
    ngx_http_performance_metrics_loc_conf_t *pmcf;
    ngx_msec_int_t response_time;
    ngx_int_t duration;
    ngx_uint_t response_code;
    ngx_str_t server_addr;
    ngx_int_t server_port;
    ngx_file_t file;
    ngx_int_t rc;
    u_char buf[256], *p;
    ngx_chain_t out;
    ngx_connection_t *c = r->connection;

    pmcf = ngx_http_get_module_loc_conf(r, ngx_http_performance_metrics_module);
    if (!pmcf->enable) {
        return;
    }

    if (c->sockaddr->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *) c->sockaddr;
        server_port = ntohs(sin->sin_port);
        server_addr.data = (u_char *) ngx_palloc(r->pool, INET_ADDRSTRLEN);
        if (server_addr.data == NULL) {
            return;
        }
        ngx_inet_ntop(AF_INET, &sin->sin_addr, server_addr.data, INET_ADDRSTRLEN);
        server_addr.len = ngx_strlen(server_addr.data);
    } else if (c->sockaddr->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) c->sockaddr;
        server_port = ntohs(sin6->sin6_port);
        server_addr.data = (u_char *) ngx_palloc(r->pool, INET6_ADDRSTRLEN);
        if (server_addr.data == NULL) {
            return;
        }
        ngx_inet_ntop(AF_INET6, &sin6->sin6_addr, server_addr.data, INET6_ADDRSTRLEN);
        server_addr.len = ngx_strlen(server_addr.data);
    } else {
        server_port = 0;
        server_addr.len = 0;
    }

    if (server_addr.len > 0) {
        server_addr.data[server_addr.len++] = ':';
        server_addr.len += ngx_sprintf(server_addr.data + server_addr.len, "%ui", server_port) - server_addr.data;
    }

    response_time = (ngx_msec_int_t) (ngx_timeofday()->msec - r->start_msec);
    duration = (ngx_int_t) (ngx_time() - r->start_sec);
    response_code = r->headers_out.status;

    p = ngx_snprintf(buf, sizeof(buf), "%V,%i,%i,%ui\n", &server_addr, response_time, duration, response_code);

    ngx_memzero(&file, sizeof(ngx_file_t));
    file.name = pmcf->output_file;
    file.log = r->connection->log;

    file.fd = ngx_open_file(pmcf->output_file.data, NGX_FILE_APPEND, NGX_FILE_CREATE_OR_OPEN, NGX_FILE_DEFAULT_ACCESS);

    if (file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, ngx_errno, ngx_open_file_n " \"%s\" failed", pmcf->output_file.data);
        return;
    }

    out.buf = ngx_create_temp_buf(r->pool, p - buf);
    if (out.buf == NULL) {
        return;
    }

    ngx_memcpy(out.buf->pos, buf, p - buf);
    out.buf->last = out.buf->pos + (p - buf);
    out.next = NULL;

    rc = ngx_write_chain_to_file(&file, &out, (off_t) 0, r->pool);

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "[PERFORMANCE_METRICS] in log function after write");

    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to write performance metrics to file");
    }

    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_errno, ngx_close_file_n " \"%s\" failed", pmcf->output_file.data);
    }
}
