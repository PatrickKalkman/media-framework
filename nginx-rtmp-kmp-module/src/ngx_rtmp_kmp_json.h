// auto-generated by generate_json_builder.py

static size_t
ngx_rtmp_kmp_connect_json_get_size(ngx_rtmp_kmp_connect_t *obj, ngx_rtmp_session_t *s)
{
    size_t result = sizeof("{\"event_type\":\"connect\",\"app\":\"") - 1 + obj->app.len + ngx_escape_json(NULL, obj->app.data, obj->app.len) +
        sizeof("\",\"flashver\":\"") - 1 + obj->flashver.len + ngx_escape_json(NULL, obj->flashver.data, obj->flashver.len) +
        sizeof("\",\"swf_url\":\"") - 1 + obj->swf_url.len + ngx_escape_json(NULL, obj->swf_url.data, obj->swf_url.len) +
        sizeof("\",\"tc_url\":\"") - 1 + obj->tc_url.len + ngx_escape_json(NULL, obj->tc_url.data, obj->tc_url.len) +
        sizeof("\",\"page_url\":\"") - 1 + obj->page_url.len + ngx_escape_json(NULL, obj->page_url.data, obj->page_url.len) +
        sizeof("\",\"addr\":\"") - 1 + s->connection->addr_text.len + ngx_escape_json(NULL, s->connection->addr_text.data, s->connection->addr_text.len) +
        sizeof("\",\"connection\":") - 1 + NGX_INT_T_LEN +
        sizeof("}") - 1;

    return result;
}

static u_char*
ngx_rtmp_kmp_connect_json_write(u_char *p, ngx_rtmp_kmp_connect_t *obj, ngx_rtmp_session_t *s)
{
    p = ngx_copy(p, "{\"event_type\":\"connect\",\"app\":\"", sizeof("{\"event_type\":\"connect\",\"app\":\"") - 1);
    p = (u_char*)ngx_escape_json(p, obj->app.data, obj->app.len);
    p = ngx_copy(p, "\",\"flashver\":\"", sizeof("\",\"flashver\":\"") - 1);
    p = (u_char*)ngx_escape_json(p, obj->flashver.data, obj->flashver.len);
    p = ngx_copy(p, "\",\"swf_url\":\"", sizeof("\",\"swf_url\":\"") - 1);
    p = (u_char*)ngx_escape_json(p, obj->swf_url.data, obj->swf_url.len);
    p = ngx_copy(p, "\",\"tc_url\":\"", sizeof("\",\"tc_url\":\"") - 1);
    p = (u_char*)ngx_escape_json(p, obj->tc_url.data, obj->tc_url.len);
    p = ngx_copy(p, "\",\"page_url\":\"", sizeof("\",\"page_url\":\"") - 1);
    p = (u_char*)ngx_escape_json(p, obj->page_url.data, obj->page_url.len);
    p = ngx_copy(p, "\",\"addr\":\"", sizeof("\",\"addr\":\"") - 1);
    p = (u_char*)ngx_escape_json(p, s->connection->addr_text.data, s->connection->addr_text.len);
    p = ngx_copy(p, "\",\"connection\":", sizeof("\",\"connection\":") - 1);
    p = ngx_sprintf(p, "%uA", (ngx_atomic_uint_t)s->connection->number);
    *p++ = '}';

    return p;
}
