/* auto-generated by generate_json_header.py */

#ifndef ngx_array_entries
#define ngx_array_entries(x)     (sizeof(x) / sizeof(x[0]))
#endif

#ifndef ngx_copy_fix
#define ngx_copy_fix(dst, src)   ngx_copy(dst, (src), sizeof(src) - 1)
#endif

#ifndef ngx_copy_str
#define ngx_copy_str(dst, src)   ngx_copy(dst, (src).data, (src).len)
#endif

/* ngx_kmp_out_upstream_json reader */

typedef struct {
    ngx_str_t   url;
    ngx_str_t   id;
    ngx_uint_t  resume_from;
    ngx_str_t   connect_data;
} ngx_kmp_out_upstream_json_t;


static ngx_json_prop_t  ngx_kmp_out_upstream_json_url = {
    ngx_string("url"),
    116079ULL,
    NGX_JSON_STRING,
    ngx_json_set_str_slot,
    offsetof(ngx_kmp_out_upstream_json_t, url),
    NULL
};


static ngx_json_prop_t  ngx_kmp_out_upstream_json_id = {
    ngx_string("id"),
    3355ULL,
    NGX_JSON_STRING,
    ngx_json_set_str_slot,
    offsetof(ngx_kmp_out_upstream_json_t, id),
    NULL
};


static ngx_json_prop_t  ngx_kmp_out_upstream_json_resume_from = {
    ngx_string("resume_from"),
    96209427719527548ULL,
    NGX_JSON_STRING,
    ngx_json_set_enum_slot,
    offsetof(ngx_kmp_out_upstream_json_t, resume_from),
    &ngx_kmp_out_resume_from_names
};


static ngx_json_prop_t  ngx_kmp_out_upstream_json_connect_data = {
    ngx_string("connect_data"),
    2609422999099421151ULL,
    NGX_JSON_STRING,
    ngx_json_set_str_slot,
    offsetof(ngx_kmp_out_upstream_json_t, connect_data),
    NULL
};


static ngx_json_prop_t  *ngx_kmp_out_upstream_json[] = {
    &ngx_kmp_out_upstream_json_id,
    &ngx_kmp_out_upstream_json_connect_data,
    NULL,
    &ngx_kmp_out_upstream_json_resume_from,
    &ngx_kmp_out_upstream_json_url,
};


/* ngx_kmp_out_upstream_republish_json writer */

static size_t
ngx_kmp_out_upstream_republish_json_get_size(ngx_kmp_out_upstream_t *obj)
{
    size_t  result;

    result =
        sizeof("\"event_type\":\"republish\",\"id\":\"") - 1 +
            ngx_json_str_get_size(&obj->id) +
        sizeof("\",\"input_id\":\"") - 1 +
            ngx_json_str_get_size(&obj->track->input_id) +
        sizeof("\",\"channel_id\":\"") - 1 +
            ngx_json_str_get_size(&obj->track->channel_id) +
        sizeof("\",\"track_id\":\"") - 1 +
            ngx_json_str_get_size(&obj->track->track_id) +
        sizeof("\"") - 1;

    return result;
}


static u_char *
ngx_kmp_out_upstream_republish_json_write(u_char *p, ngx_kmp_out_upstream_t
    *obj)
{
    p = ngx_copy_fix(p, "\"event_type\":\"republish\",\"id\":\"");
    p = ngx_json_str_write(p, &obj->id);
    p = ngx_copy_fix(p, "\",\"input_id\":\"");
    p = ngx_json_str_write(p, &obj->track->input_id);
    p = ngx_copy_fix(p, "\",\"channel_id\":\"");
    p = ngx_json_str_write(p, &obj->track->channel_id);
    p = ngx_copy_fix(p, "\",\"track_id\":\"");
    p = ngx_json_str_write(p, &obj->track->track_id);
    *p++ = '\"';

    return p;
}


/* ngx_kmp_out_upstream_json writer */

size_t
ngx_kmp_out_upstream_json_get_size(ngx_kmp_out_upstream_t *obj)
{
    size_t  result;

    result =
        sizeof("{\"id\":\"") - 1 + ngx_json_str_get_size(&obj->id) +
        sizeof("\",\"remote_addr\":\"") - 1 +
            ngx_json_str_get_size(&obj->remote_addr) +
        sizeof("\",\"local_addr\":\"") - 1 +
            ngx_json_str_get_size(&obj->local_addr) +
        sizeof("\",\"connection\":") - 1 + NGX_INT_T_LEN +
        sizeof(",\"resume_from\":\"") - 1 +
            ngx_kmp_out_resume_from_names[obj->resume_from].len +
        sizeof("\",\"sent_bytes\":") - 1 + NGX_OFF_T_LEN +
        sizeof(",\"position\":") - 1 + NGX_OFF_T_LEN +
        sizeof(",\"acked_frames\":") - 1 + NGX_INT64_LEN +
        sizeof(",\"acked_bytes\":") - 1 + NGX_OFF_T_LEN +
        sizeof(",\"auto_acked_frames\":") - 1 + NGX_INT_T_LEN +
        sizeof("}") - 1;

    return result;
}


u_char *
ngx_kmp_out_upstream_json_write(u_char *p, ngx_kmp_out_upstream_t *obj)
{
    p = ngx_copy_fix(p, "{\"id\":\"");
    p = ngx_json_str_write(p, &obj->id);
    p = ngx_copy_fix(p, "\",\"remote_addr\":\"");
    p = ngx_json_str_write(p, &obj->remote_addr);
    p = ngx_copy_fix(p, "\",\"local_addr\":\"");
    p = ngx_json_str_write(p, &obj->local_addr);
    p = ngx_copy_fix(p, "\",\"connection\":");
    p = ngx_sprintf(p, "%uA", (ngx_atomic_uint_t) obj->log.connection);
    p = ngx_copy_fix(p, ",\"resume_from\":\"");
    p = ngx_sprintf(p, "%V", &ngx_kmp_out_resume_from_names[obj->resume_from]);
    p = ngx_copy_fix(p, "\",\"sent_bytes\":");
    p = ngx_sprintf(p, "%O", (off_t) (obj->peer.connection ?
        obj->peer.connection->sent : 0));
    p = ngx_copy_fix(p, ",\"position\":");
    p = ngx_sprintf(p, "%O", (off_t) (obj->peer.connection ? obj->sent_base +
        obj->peer.connection->sent : 0));
    p = ngx_copy_fix(p, ",\"acked_frames\":");
    p = ngx_sprintf(p, "%uL", (uint64_t) obj->acked_frame_id -
        obj->track->connect.c.initial_frame_id);
    p = ngx_copy_fix(p, ",\"acked_bytes\":");
    p = ngx_sprintf(p, "%O", (off_t) obj->acked_bytes);
    p = ngx_copy_fix(p, ",\"auto_acked_frames\":");
    p = ngx_sprintf(p, "%ui", (ngx_uint_t) obj->auto_acked_frames);
    *p++ = '}';

    return p;
}
