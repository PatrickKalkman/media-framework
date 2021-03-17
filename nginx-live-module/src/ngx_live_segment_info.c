#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_live.h"
#include "ngx_live_segment_info.h"


#define NGX_LIVE_SEGMENT_INFO_PERSIST_BLOCK  (0x666e6773)  /* sgnf */

/* sizeof ngx_live_segment_info_node_t = 512 */
#define NGX_LIVE_SEGMENT_INFO_NODE_ELTS    (56)

#define NGX_LIVE_SEGMENT_INFO_FREE_PERIOD  (32)

enum {
    NGX_LIVE_BP_SEGMENT_INFO_NODE,

    NGX_LIVE_BP_COUNT
};


typedef struct {
    ngx_flag_t                     gaps;
    ngx_flag_t                     bitrate;
    ngx_uint_t                     bitrate_lower_bound;
    ngx_uint_t                     bitrate_upper_bound;
    ngx_uint_t                     bp_idx[NGX_LIVE_BP_COUNT];
} ngx_live_segment_info_preset_conf_t;


struct ngx_live_segment_info_elt_s {
    uint32_t                       index;
    uint32_t                       bitrate;
};

struct ngx_live_segment_info_node_s {
    ngx_rbtree_node_t              node;        /* key = segment_index */
    ngx_queue_t                    queue;
    ngx_uint_t                     nelts;
    ngx_live_segment_info_elt_t    elts[NGX_LIVE_SEGMENT_INFO_NODE_ELTS];
};

typedef struct {
    ngx_rbtree_t                   rbtree;
    ngx_rbtree_node_t              sentinel;
    ngx_queue_t                    queue;
    uint64_t                       last_segment_bitrate;    /* bitrate * 100 */
    uint32_t                       initial_bitrate;
} ngx_live_segment_info_track_ctx_t;

typedef struct {
    uint32_t                       min_free_index;
} ngx_live_segment_info_channel_ctx_t;


static ngx_int_t ngx_live_segment_info_preconfiguration(ngx_conf_t *cf);
static ngx_int_t ngx_live_segment_info_postconfiguration(ngx_conf_t *cf);

static void *ngx_live_segment_info_create_preset_conf(ngx_conf_t *cf);
static char *ngx_live_segment_info_merge_preset_conf(ngx_conf_t *cf,
    void *parent, void *child);


static ngx_command_t  ngx_live_segment_info_commands[] = {

    { ngx_string("segment_info_gaps"),
      NGX_LIVE_MAIN_CONF|NGX_LIVE_PRESET_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_LIVE_PRESET_CONF_OFFSET,
      offsetof(ngx_live_segment_info_preset_conf_t, gaps),
      NULL },

    { ngx_string("segment_info_bitrate"),
      NGX_LIVE_MAIN_CONF|NGX_LIVE_PRESET_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_LIVE_PRESET_CONF_OFFSET,
      offsetof(ngx_live_segment_info_preset_conf_t, bitrate),
      NULL },

    { ngx_string("segment_info_bitrate_lower_bound"),
      NGX_LIVE_MAIN_CONF|NGX_LIVE_PRESET_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_LIVE_PRESET_CONF_OFFSET,
      offsetof(ngx_live_segment_info_preset_conf_t, bitrate_lower_bound),
      NULL },

    { ngx_string("segment_info_bitrate_upper_bound"),
      NGX_LIVE_MAIN_CONF|NGX_LIVE_PRESET_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_LIVE_PRESET_CONF_OFFSET,
      offsetof(ngx_live_segment_info_preset_conf_t, bitrate_upper_bound),
      NULL },

      ngx_null_command
};

static ngx_live_module_t  ngx_live_segment_info_module_ctx = {
    ngx_live_segment_info_preconfiguration,   /* preconfiguration */
    ngx_live_segment_info_postconfiguration,  /* postconfiguration */

    NULL,                                     /* create main configuration */
    NULL,                                     /* init main configuration */

    ngx_live_segment_info_create_preset_conf, /* create preset configuration */
    ngx_live_segment_info_merge_preset_conf,  /* merge preset configuration */
};

ngx_module_t  ngx_live_segment_info_module = {
    NGX_MODULE_V1,
    &ngx_live_segment_info_module_ctx,        /* module context */
    ngx_live_segment_info_commands,           /* module directives */
    NGX_LIVE_MODULE,                          /* module type */
    NULL,                                     /* init master */
    NULL,                                     /* init module */
    NULL,                                     /* init process */
    NULL,                                     /* init thread */
    NULL,                                     /* exit thread */
    NULL,                                     /* exit process */
    NULL,                                     /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_live_segment_info_elt_t *
ngx_live_segment_info_push(ngx_live_channel_t *channel,
    ngx_live_segment_info_track_ctx_t *ctx)
{
    uint32_t                              segment_index;
    ngx_queue_t                          *q;
    ngx_live_segment_info_elt_t          *elt;
    ngx_live_segment_info_node_t         *last;
    ngx_live_segment_info_preset_conf_t  *sipcf;

    segment_index = channel->next_segment_index;

    q = ngx_queue_last(&ctx->queue);
    if (q != ngx_queue_sentinel(&ctx->queue)) {

        last = ngx_queue_data(q, ngx_live_segment_info_node_t, queue);

        if (last->nelts < NGX_LIVE_SEGMENT_INFO_NODE_ELTS) {

            elt = &last->elts[last->nelts];
            last->nelts++;

            elt->index = segment_index;
            return elt;
        }
    }

    sipcf = ngx_live_get_module_preset_conf(channel,
        ngx_live_segment_info_module);

    last = ngx_block_pool_alloc(channel->block_pool,
        sipcf->bp_idx[NGX_LIVE_BP_SEGMENT_INFO_NODE]);
    if (last == NULL) {
        return NULL;
    }

    last->node.key = segment_index;

    ngx_queue_insert_tail(&ctx->queue, &last->queue);
    ngx_rbtree_insert(&ctx->rbtree, &last->node);

    elt = &last->elts[0];
    last->nelts = 1;

    elt->index = segment_index;
    return elt;
}

static ngx_int_t
ngx_live_segment_info_segment_created(ngx_live_channel_t *channel, void *ectx)
{
    uint64_t                              cur;
    uint64_t                              last;
    ngx_queue_t                          *q;
    ngx_live_track_t                     *cur_track;
    ngx_live_segment_info_elt_t          *elt;
    ngx_live_segment_info_track_ctx_t    *cur_ctx;
    ngx_live_segment_info_preset_conf_t  *sipcf;

    sipcf = ngx_live_get_module_preset_conf(channel,
        ngx_live_segment_info_module);

    if (!sipcf->bitrate && !sipcf->gaps) {
        return NGX_OK;
    }

    for (q = ngx_queue_head(&channel->tracks.queue);
        q != ngx_queue_sentinel(&channel->tracks.queue);
        q = ngx_queue_next(q))
    {
        cur_track = ngx_queue_data(q, ngx_live_track_t, queue);

        cur_ctx = ngx_live_get_module_ctx(cur_track,
            ngx_live_segment_info_module);

        cur = cur_track->last_segment_bitrate;
        if (cur) {
            if (!sipcf->bitrate) {
                cur = NGX_LIVE_SEGMENT_NO_BITRATE;
            }

        } else {
            if (!sipcf->gaps) {
                continue;
            }
        }

        last = cur_ctx->last_segment_bitrate;
        if (last >= cur * sipcf->bitrate_lower_bound &&
            last <= cur * sipcf->bitrate_upper_bound)
        {
            continue;
        }

        elt = ngx_live_segment_info_push(channel, cur_ctx);
        if (elt == NULL) {
            ngx_log_error(NGX_LOG_NOTICE, &cur_track->log, 0,
                "ngx_live_segment_info_segment_created: push failed");
            return NGX_ERROR;
        }

        elt->bitrate = cur;

        cur_ctx->last_segment_bitrate = cur * 100;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_live_segment_info_track_copy(ngx_live_track_t *dst, void *ectx)
{
    ngx_queue_t                          *q;
    ngx_live_track_t                     *src = ectx;
    ngx_live_channel_t                   *dst_channel;
    ngx_live_segment_info_node_t         *src_node;
    ngx_live_segment_info_node_t         *dst_node;
    ngx_live_segment_info_track_ctx_t    *src_ctx;
    ngx_live_segment_info_track_ctx_t    *dst_ctx;
    ngx_live_segment_info_preset_conf_t  *sipcf;

    src_ctx = ngx_live_get_module_ctx(src, ngx_live_segment_info_module);
    dst_ctx = ngx_live_get_module_ctx(dst, ngx_live_segment_info_module);

    dst_channel = dst->channel;
    sipcf = ngx_live_get_module_preset_conf(dst_channel,
        ngx_live_segment_info_module);

    for (q = ngx_queue_head(&src_ctx->queue);
        q != ngx_queue_sentinel(&src_ctx->queue);
        q = ngx_queue_next(q))
    {
        dst_node = ngx_block_pool_alloc(dst_channel->block_pool,
            sipcf->bp_idx[NGX_LIVE_BP_SEGMENT_INFO_NODE]);
        if (dst_node == NULL) {
            return NGX_ERROR;
        }

        src_node = ngx_queue_data(q, ngx_live_segment_info_node_t, queue);

        dst_node->node.key = src_node->node.key;
        dst_node->nelts = src_node->nelts;
        ngx_memcpy(dst_node->elts, src_node->elts,
            sizeof(src_node->elts[0]) * src_node->nelts);

        ngx_queue_insert_tail(&dst_ctx->queue, &dst_node->queue);
        ngx_rbtree_insert(&dst_ctx->rbtree, &dst_node->node);
    }

    dst_ctx->last_segment_bitrate = src_ctx->last_segment_bitrate;

    return NGX_OK;
}

static ngx_int_t
ngx_live_segment_info_track_segment_free(ngx_live_track_t *track,
    uint32_t min_segment_index)
{
    ngx_queue_t                          *q, *next;
    ngx_live_channel_t                   *channel;
    ngx_live_segment_info_node_t         *next_node;
    ngx_live_segment_info_node_t         *node;
    ngx_live_segment_info_track_ctx_t    *ctx;
    ngx_live_segment_info_preset_conf_t  *sipcf;

    channel = track->channel;
    ctx = ngx_live_get_module_ctx(track, ngx_live_segment_info_module);
    sipcf = ngx_live_get_module_preset_conf(channel,
        ngx_live_segment_info_module);

    q = ngx_queue_head(&ctx->queue);
    for ( ;; ) {

        next = ngx_queue_next(q);
        if (next == ngx_queue_sentinel(&ctx->queue)) {
            break;
        }

        next_node = ngx_queue_data(next, ngx_live_segment_info_node_t, queue);
        if (min_segment_index < next_node->node.key) {
            break;
        }

        node = ngx_queue_data(q, ngx_live_segment_info_node_t, queue);
        ngx_queue_remove(q);
        ngx_rbtree_delete(&ctx->rbtree, &node->node);

        ngx_block_pool_free(channel->block_pool,
            sipcf->bp_idx[NGX_LIVE_BP_SEGMENT_INFO_NODE], node);

        q = next;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_live_segment_info_segment_free(ngx_live_channel_t *channel, void *ectx)
{
    uint32_t                              min_segment_index = (uintptr_t) ectx;
    ngx_queue_t                          *q;
    ngx_live_track_t                     *cur_track;
    ngx_live_segment_info_channel_ctx_t  *cctx;

    /* no need to look for nodes to free on each segment */
    cctx = ngx_live_get_module_ctx(channel, ngx_live_segment_info_module);

    if (min_segment_index < cctx->min_free_index) {
        return NGX_OK;
    }

    cctx->min_free_index = min_segment_index +
        NGX_LIVE_SEGMENT_INFO_FREE_PERIOD;

    /* free unused media info nodes */
    for (q = ngx_queue_head(&channel->tracks.queue);
        q != ngx_queue_sentinel(&channel->tracks.queue);
        q = ngx_queue_next(q))
    {
        cur_track = ngx_queue_data(q, ngx_live_track_t, queue);

        ngx_live_segment_info_track_segment_free(cur_track,
            min_segment_index);
    }

    return NGX_OK;
}

static ngx_live_segment_info_node_t *
ngx_live_segment_info_lookup(ngx_live_segment_info_track_ctx_t *ctx,
    uint32_t segment_index)
{
    ngx_queue_t                   *prev;
    ngx_rbtree_t                  *rbtree;
    ngx_rbtree_node_t             *rbnode;
    ngx_rbtree_node_t             *sentinel;
    ngx_rbtree_node_t             *next_node;
    ngx_live_segment_info_node_t  *node;

    rbtree = &ctx->rbtree;
    rbnode = rbtree->root;
    sentinel = rbtree->sentinel;

    if (rbnode == sentinel) {
        return NULL;
    }

    for ( ;; ) {

        next_node = (segment_index < rbnode->key) ? rbnode->left :
            rbnode->right;
        if (next_node == sentinel) {
            break;
        }

        rbnode = next_node;
    }

    node = (ngx_live_segment_info_node_t *) rbnode;
    if (segment_index < node->node.key) {

        /* Note: since we don't know the end index of each node, it is possible
            that we made a wrong right turn, in that case, we need to go back
            one node */
        prev = ngx_queue_prev(&node->queue);
        if (prev == ngx_queue_sentinel(&ctx->queue)) {
            return node;
        }

        node = ngx_queue_data(prev, ngx_live_segment_info_node_t, queue);
    }

    return node;
}


void
ngx_live_segment_info_iter_init(ngx_live_segment_info_iter_t *iter,
    ngx_live_track_t *track, uint32_t segment_index)
{
    ngx_live_segment_info_track_ctx_t  *ctx;

    ctx = ngx_live_get_module_ctx(track, ngx_live_segment_info_module);

    iter->bitrate = ctx->initial_bitrate;

    iter->node = ngx_live_segment_info_lookup(ctx, segment_index);
    if (iter->node == NULL) {
        return;
    }

    iter->sentinel = ngx_queue_sentinel(&ctx->queue);
    iter->cur = iter->node->elts;
    iter->last = iter->cur + iter->node->nelts;
}

uint32_t
ngx_live_segment_info_iter_next(ngx_live_segment_info_iter_t *iter,
    uint32_t segment_index)
{
    ngx_queue_t  *next;

    if (iter->node == NULL) {
        return iter->bitrate;
    }

    while (iter->cur->index <= segment_index) {

        iter->bitrate = iter->cur->bitrate;

        iter->cur++;
        if (iter->cur < iter->last) {
            continue;
        }

        next = ngx_queue_next(&iter->node->queue);
        if (next == iter->sentinel) {
            iter->node = NULL;
            break;
        }

        iter->node = ngx_queue_data(next, ngx_live_segment_info_node_t,
            queue);

        iter->cur = iter->node->elts;
        iter->last = iter->cur + iter->node->nelts;
    }

    return iter->bitrate;
}


void
ngx_live_segment_info_count(ngx_live_track_t *track, uint32_t first_index,
    uint32_t last_index, uint32_t *bitrate_count, uint32_t *gap_count)
{
    uint32_t                            prev_index;
    uint32_t                            prev_bitrate;
    ngx_queue_t                        *next;
    ngx_live_segment_info_elt_t        *cur, *last;
    ngx_live_segment_info_node_t       *node;
    ngx_live_segment_info_track_ctx_t  *ctx;

    ctx = ngx_live_get_module_ctx(track, ngx_live_segment_info_module);

    prev_index = first_index;
    prev_bitrate = ctx->initial_bitrate;

    node = ngx_live_segment_info_lookup(ctx, first_index);
    if (node == NULL) {
        goto done;
    }

    cur = node->elts;
    last = cur + node->nelts;

    for ( ;; ) {

        if (cur->index > prev_index) {

            if (cur->index >= last_index) {
                break;
            }

            if (prev_bitrate == 0) {
                *gap_count += cur->index - prev_index;

            } else if (prev_bitrate != NGX_LIVE_SEGMENT_NO_BITRATE) {
                (*bitrate_count)++;
            }

            prev_index = cur->index;
        }

        prev_bitrate = cur->bitrate;

        cur++;
        if (cur < last) {
            continue;
        }

        next = ngx_queue_next(&node->queue);
        if (next == ngx_queue_sentinel(&ctx->queue)) {
            break;
        }

        node = ngx_queue_data(next, ngx_live_segment_info_node_t, queue);

        cur = node->elts;
        last = cur + node->nelts;
    }

done:

    if (prev_bitrate == 0) {
        *gap_count += last_index - prev_index;

    } else if (prev_bitrate != NGX_LIVE_SEGMENT_NO_BITRATE) {
        (*bitrate_count)++;
    }
}


static ngx_int_t
ngx_live_segment_info_channel_init(ngx_live_channel_t *channel, void *ectx)
{
    ngx_live_segment_info_channel_ctx_t  *cctx;

    cctx = ngx_pcalloc(channel->pool, sizeof(*cctx));
    if (cctx == NULL) {
        ngx_log_error(NGX_LOG_NOTICE, &channel->log, 0,
            "ngx_live_segment_info_channel_init: alloc failed");
        return NGX_ERROR;
    }

    ngx_live_set_ctx(channel, cctx, ngx_live_segment_info_module);

    return NGX_OK;
}

static ngx_int_t
ngx_live_segment_info_track_init(ngx_live_track_t *track, void *ectx)
{
    ngx_live_segment_info_track_ctx_t    *ctx;
    ngx_live_segment_info_preset_conf_t  *sipcf;

    ctx = ngx_live_get_module_ctx(track, ngx_live_segment_info_module);

    sipcf = ngx_live_get_module_preset_conf(track->channel,
        ngx_live_segment_info_module);

    ngx_rbtree_init(&ctx->rbtree, &ctx->sentinel, ngx_rbtree_insert_value);
    ngx_queue_init(&ctx->queue);

    if (!sipcf->gaps) {
        ctx->initial_bitrate = NGX_LIVE_SEGMENT_NO_BITRATE;
        ctx->last_segment_bitrate = NGX_LIVE_SEGMENT_NO_BITRATE * 100;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_live_segment_info_track_free(ngx_live_track_t *track, void *ectx)
{
    ngx_queue_t                          *q;
    ngx_live_channel_t                   *channel;
    ngx_live_segment_info_node_t         *node;
    ngx_live_segment_info_track_ctx_t    *ctx;
    ngx_live_segment_info_preset_conf_t  *sipcf;

    ctx = ngx_live_get_module_ctx(track, ngx_live_segment_info_module);

    q = ngx_queue_head(&ctx->queue);
    if (q == NULL) {
        /* init wasn't called */
        return NGX_OK;
    }

    channel = track->channel;
    sipcf = ngx_live_get_module_preset_conf(channel,
        ngx_live_segment_info_module);

    while (q != ngx_queue_sentinel(&ctx->queue)) {

        node = ngx_queue_data(q, ngx_live_segment_info_node_t, queue);

        q = ngx_queue_next(q);      /* move to next before freeing */

        ngx_block_pool_free(channel->block_pool,
            sipcf->bp_idx[NGX_LIVE_BP_SEGMENT_INFO_NODE], node);
    }

    return NGX_OK;
}

static ngx_int_t
ngx_live_segment_info_write_index(ngx_persist_write_ctx_t *write_ctx,
    void *obj)
{
    ngx_queue_t                        *q;
    ngx_live_track_t                   *track = obj;
    ngx_live_persist_snap_t            *snap;
    ngx_live_segment_info_elt_t        *first, *last;
    ngx_live_segment_info_node_t       *node;
    ngx_live_segment_info_track_ctx_t  *ctx;

    ctx = ngx_live_get_module_ctx(track, ngx_live_segment_info_module);
    snap = ngx_persist_write_ctx(write_ctx);

    if (snap->scope.min_index > 0) {
        node = ngx_live_segment_info_lookup(ctx, snap->scope.min_index);
        if (node == NULL) {
            return NGX_OK;
        }

        q = &node->queue;

    } else {
        q = ngx_queue_head(&ctx->queue);
        if (q == ngx_queue_sentinel(&ctx->queue)) {
            return NGX_OK;
        }

        node = ngx_queue_data(q, ngx_live_segment_info_node_t, queue);
    }

    if (ngx_persist_write_block_open(write_ctx,
        NGX_LIVE_SEGMENT_INFO_PERSIST_BLOCK) != NGX_OK)
    {
        return NGX_ERROR;
    }

    ngx_persist_write_block_set_header(write_ctx, 0);

    for ( ;; ) {

        first = &node->elts[0];
        last = &node->elts[node->nelts];

        /* trim the left side */
        for ( ;; ) {
            if (first->index >= snap->scope.min_index) {
                break;
            }

            first++;

            if (first >= last) {
                goto next;
            }
        }

        /* trim the right side */
        for ( ;; ) {
            if (last[-1].index <= snap->scope.max_index) {
                break;
            }

            last--;

            if (first >= last) {
                goto next;
            }
        }

        if (ngx_persist_write_append(write_ctx, first,
            (u_char *) last - (u_char *) first) != NGX_OK)
        {
            ngx_log_error(NGX_LOG_NOTICE, &track->log, 0,
                "ngx_live_segment_info_write_index: append failed");
            return NGX_ERROR;
        }

    next:

        q = ngx_queue_next(q);
        if (q == ngx_queue_sentinel(&ctx->queue)) {
            break;
        }

        node = ngx_queue_data(q, ngx_live_segment_info_node_t, queue);
    }

    ngx_persist_write_block_close(write_ctx);

    return NGX_OK;
}

static ngx_int_t
ngx_live_segment_info_read_index(ngx_persist_block_header_t *block,
    ngx_mem_rstream_t *rs, void *obj)
{
    uint32_t                              left;
    uint32_t                              count;
    uint32_t                              min_index;
    ngx_str_t                             data;
    ngx_live_track_t                     *track = obj;
    ngx_live_channel_t                   *channel;
    ngx_live_segment_info_elt_t          *dst;
    ngx_live_segment_info_elt_t          *cur, *next;
    ngx_live_segment_info_node_t         *last;
    ngx_live_persist_index_scope_t       *scope;
    ngx_live_segment_info_track_ctx_t    *ctx;
    ngx_live_segment_info_preset_conf_t  *sipcf;

    if (ngx_persist_read_skip_block_header(rs, block) != NGX_OK) {
        return NGX_BAD_DATA;
    }

    ngx_mem_rstream_get_left(rs, &data);

    left = data.len / sizeof(*cur);
    if (left <= 0) {
        return NGX_OK;
    }

    channel = track->channel;
    ctx = ngx_live_get_module_ctx(track, ngx_live_segment_info_module);
    sipcf = ngx_live_get_module_preset_conf(channel,
        ngx_live_segment_info_module);
    scope = ngx_mem_rstream_scope(rs);

    min_index = scope->min_index;

    cur = (void *) data.data;

    if (!ngx_queue_empty(&ctx->queue)) {

        /* append to existing node */
        last = ngx_queue_data(ngx_queue_last(&ctx->queue),
            ngx_live_segment_info_node_t, queue);

        dst = last->elts + last->nelts;
        if (dst[-1].index >= min_index) {
            /* can happen due to duplicate block */
            ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                "ngx_live_segment_info_read_index: "
                "last index %uD exceeds min index %uD",
                dst[-1].index, min_index);
            return NGX_BAD_DATA;
        }

        count = NGX_LIVE_SEGMENT_INFO_NODE_ELTS - last->nelts;
        if (count > left) {
            count = left;
        }

        last->nelts += count;
        next = cur + count;

        for (; cur < next; cur++) {
            if (cur->index < min_index) {
                ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                    "ngx_live_segment_info_read_index: "
                    "segment index %uD less than min segment index %uD",
                    cur->index, min_index);
                return NGX_BAD_DATA;
            }

            if (cur->index > scope->max_index) {
                ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                    "ngx_live_segment_info_read_index: "
                    "segment index %uD greater than max segment index %uD",
                    cur->index, scope->max_index);
                return NGX_BAD_DATA;
            }

            min_index = cur->index + 1;

            *dst++ = *cur;
        }

        left -= count;
    }

    /* create new nodes */
    while (left > 0) {

        last = ngx_block_pool_alloc(channel->block_pool,
            sipcf->bp_idx[NGX_LIVE_BP_SEGMENT_INFO_NODE]);
        if (last == NULL) {
            return NGX_ERROR;
        }

        last->node.key = cur->index;

        ngx_queue_insert_tail(&ctx->queue, &last->queue);
        ngx_rbtree_insert(&ctx->rbtree, &last->node);

        dst = last->elts;

        last->nelts = ngx_min(left, NGX_LIVE_SEGMENT_INFO_NODE_ELTS);
        next = cur + last->nelts;

        for (; cur < next; cur++) {
            if (cur->index < min_index) {
                ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                    "ngx_live_segment_info_read_index: "
                    "segment index %uD less than min segment index %uD",
                    cur->index, min_index);
                return NGX_BAD_DATA;
            }

            if (cur->index > scope->max_index) {
                ngx_log_error(NGX_LOG_ERR, rs->log, 0,
                    "ngx_live_segment_info_read_index: "
                    "segment index %uD greater than max segment index %uD",
                    cur->index, scope->max_index);
                return NGX_BAD_DATA;
            }

            min_index = cur->index + 1;

            *dst++ = *cur;
        }

        left -= last->nelts;
    }

    ctx->last_segment_bitrate = last->elts[last->nelts - 1].bitrate * 100;

    return NGX_OK;
}


static ngx_int_t
ngx_live_segment_info_write_serve(ngx_persist_write_ctx_t *write_ctx,
    void *obj)
{
    ngx_queue_t                        *q;
    ngx_live_track_t                   *track;
    ngx_live_segment_info_elt_t         elt;
    ngx_live_segment_info_elt_t        *first, *last;
    ngx_live_segment_info_node_t       *node;
    ngx_live_persist_serve_scope_t     *scope;
    ngx_live_segment_info_track_ctx_t  *ctx;

    scope = ngx_persist_write_ctx(write_ctx);
    if (!(scope->flags & NGX_LIVE_SERVE_SEGMENT_INFO)) {
        return NGX_OK;
    }

    track = obj;
    ctx = ngx_live_get_module_ctx(track, ngx_live_segment_info_module);

    if (ngx_persist_write_block_open(write_ctx,
            NGX_LIVE_SEGMENT_INFO_PERSIST_BLOCK) != NGX_OK)
    {
        return NGX_ERROR;
    }

    ngx_persist_write_block_set_header(write_ctx, 0);

    /* TODO: save only the minimum according to the manifest timeline in scope,
        and scope->segment_index (need to save one node before each period). */

    q = ngx_queue_head(&ctx->queue);
    if (q == ngx_queue_sentinel(&ctx->queue)) {
        elt.index = 0;
        elt.bitrate = ctx->initial_bitrate;

        if (ngx_persist_write(write_ctx, &elt, sizeof(elt)) != NGX_OK) {
            return NGX_ERROR;
        }

        goto done;
    }

    node = ngx_queue_data(q, ngx_live_segment_info_node_t, queue);
    if (node->elts[0].index > 0) {
        elt.index = 0;
        elt.bitrate = ctx->initial_bitrate;

        if (ngx_persist_write(write_ctx, &elt, sizeof(elt)) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    for ( ;; ) {
        first = &node->elts[0];
        last = &node->elts[node->nelts];

        if (ngx_persist_write_append(write_ctx, first,
            (u_char *) last - (u_char *) first) != NGX_OK)
        {
            ngx_log_error(NGX_LOG_NOTICE, &track->log, 0,
                "ngx_live_segment_info_write_serve: append failed");
            return NGX_ERROR;
        }

        q = ngx_queue_next(q);
        if (q == ngx_queue_sentinel(&ctx->queue)) {
            break;
        }

        node = ngx_queue_data(q, ngx_live_segment_info_node_t, queue);
    }

done:

    ngx_persist_write_block_close(write_ctx);

    return NGX_OK;
}


static ngx_persist_block_t  ngx_live_segment_info_blocks[] = {
    /*
     * persist data:
     *   ngx_live_segment_info_elt_t  info[];
     */
    { NGX_LIVE_SEGMENT_INFO_PERSIST_BLOCK, NGX_LIVE_PERSIST_CTX_INDEX_TRACK, 0,
      ngx_live_segment_info_write_index,
      ngx_live_segment_info_read_index },

    /*
     * persist data:
     *   ngx_live_segment_info_elt_t  info[];
     */
    { NGX_LIVE_SEGMENT_INFO_PERSIST_BLOCK, NGX_LIVE_PERSIST_CTX_SERVE_TRACK, 0,
      ngx_live_segment_info_write_serve, NULL },

    ngx_null_persist_block
};

static ngx_int_t
ngx_live_segment_info_preconfiguration(ngx_conf_t *cf)
{
    if (ngx_live_persist_add_blocks(cf, ngx_live_segment_info_blocks)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_live_channel_event_t  ngx_live_segment_info_channel_events[] = {
    { ngx_live_segment_info_channel_init, NGX_LIVE_EVENT_CHANNEL_INIT },
    { ngx_live_segment_info_segment_created,
        NGX_LIVE_EVENT_CHANNEL_SEGMENT_CREATED },
    { ngx_live_segment_info_segment_free,
        NGX_LIVE_EVENT_CHANNEL_SEGMENT_FREE },
      ngx_live_null_event
};

static ngx_live_track_event_t    ngx_live_segment_info_track_events[] = {
    { ngx_live_segment_info_track_init, NGX_LIVE_EVENT_TRACK_INIT },
    { ngx_live_segment_info_track_free, NGX_LIVE_EVENT_TRACK_FREE },
    { ngx_live_segment_info_track_copy, NGX_LIVE_EVENT_TRACK_COPY },
      ngx_live_null_event
};

static ngx_int_t
ngx_live_segment_info_postconfiguration(ngx_conf_t *cf)
{
    if (ngx_live_core_channel_events_add(cf,
        ngx_live_segment_info_channel_events) != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_live_core_track_events_add(cf,
        ngx_live_segment_info_track_events) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static void *
ngx_live_segment_info_create_preset_conf(ngx_conf_t *cf)
{
    ngx_live_segment_info_preset_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_live_segment_info_preset_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->gaps = NGX_CONF_UNSET;
    conf->bitrate = NGX_CONF_UNSET;
    conf->bitrate_lower_bound = NGX_CONF_UNSET_UINT;
    conf->bitrate_upper_bound = NGX_CONF_UNSET_UINT;

    return conf;
}

static char *
ngx_live_segment_info_merge_preset_conf(ngx_conf_t *cf, void *parent,
    void *child)
{
    ngx_live_segment_info_preset_conf_t  *prev = parent;
    ngx_live_segment_info_preset_conf_t  *conf = child;

    ngx_conf_merge_value(conf->gaps,
                         prev->gaps, 1);

    ngx_conf_merge_value(conf->bitrate,
                         prev->bitrate, 0);

    ngx_conf_merge_uint_value(conf->bitrate_lower_bound,
                              prev->bitrate_lower_bound, 90);

    ngx_conf_merge_uint_value(conf->bitrate_upper_bound,
                              prev->bitrate_upper_bound, 110);

    if (ngx_live_core_add_block_pool_index(cf,
        &conf->bp_idx[NGX_LIVE_BP_SEGMENT_INFO_NODE],
        sizeof(ngx_live_segment_info_node_t)) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_live_reserve_track_ctx_size(cf, ngx_live_segment_info_module,
        sizeof(ngx_live_segment_info_track_ctx_t)) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
