#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <ttypt/corm.h>
#include <ttypt/cormr.h>
#include <ttypt/joint.h>

/* CormStore wrapper structure */
typedef struct {
    uint32_t hd;
    bool is_open;
    char path[256];
} CormStoreWrapper;

/* Finalizer for CormStore */
static void CormStoreFinalizer(napi_env env, void* finalize_data, void* finalize_hint) {
    (void)env;
    (void)finalize_hint;
    CormStoreWrapper* wrap = (CormStoreWrapper*)finalize_data;
    if (wrap) {
        if (wrap->is_open && wrap->hd != 0 && wrap->hd != CM_MISS) {
            corm_close(wrap->hd);
            wrap->is_open = false;
        }
        free(wrap);
    }
}

/* CormStore.open(path, databaseName) */
static napi_value CormOpen(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_status status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 1) {
        napi_throw_type_error(env, NULL, "Expected at least 1 argument: (path, optionalDbName)");
        return NULL;
    }

    char path_buf[256] = {0};
    size_t path_len = 0;
    napi_get_value_string_utf8(env, args[0], path_buf, sizeof(path_buf) - 1, &path_len);

    char db_name[64] = "duna";
    if (argc >= 2) {
        size_t db_len = 0;
        napi_get_value_string_utf8(env, args[1], db_name, sizeof(db_name) - 1, &db_len);
    }

    /* Open CM_STR -> CM_STR map with auto-index and file backing */
    uint32_t hd = corm_open(path_buf[0] ? path_buf : NULL,
                            db_name,
                            CM_STR,
                            CM_STR,
                            0xFF,
                            CM_AINDEX | CM_SORTED);

    if (hd == CM_MISS || hd == 0) {
        napi_throw_error(env, NULL, "Failed to open or create corm database");
        return NULL;
    }

    CormStoreWrapper* wrap = (CormStoreWrapper*)calloc(1, sizeof(CormStoreWrapper));
    wrap->hd = hd;
    wrap->is_open = true;
    strncpy(wrap->path, path_buf, sizeof(wrap->path) - 1);

    napi_value result;
    napi_create_object(env, &result);

    napi_value hd_val;
    napi_create_uint32(env, hd, &hd_val);
    napi_set_named_property(env, result, "handle", hd_val);

    napi_wrap(env, result, wrap, CormStoreFinalizer, NULL, NULL);
    return result;
}

/* CormStore.put(storeObj, key, value) */
static napi_value CormPut(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_status status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 3) {
        napi_throw_type_error(env, NULL, "Expected (storeObj, key, value)");
        return NULL;
    }

    CormStoreWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Corm store is closed or invalid");
        return NULL;
    }

    char key_buf[128] = {0};
    size_t key_len = 0;
    napi_get_value_string_utf8(env, args[1], key_buf, sizeof(key_buf) - 1, &key_len);

    char val_buf[4096] = {0};
    size_t val_len = 0;
    napi_get_value_string_utf8(env, args[2], val_buf, sizeof(val_buf) - 1, &val_len);

    uint32_t pos = corm_put(wrap->hd, key_buf, val_buf);

    napi_value res;
    napi_create_uint32(env, pos, &res);
    return res;
}

/* CormStore.get(storeObj, key) */
static napi_value CormGet(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_status status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 2) {
        napi_throw_type_error(env, NULL, "Expected (storeObj, key)");
        return NULL;
    }

    CormStoreWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Corm store is closed or invalid");
        return NULL;
    }

    char key_buf[128] = {0};
    size_t key_len = 0;
    napi_get_value_string_utf8(env, args[1], key_buf, sizeof(key_buf) - 1, &key_len);

    const void* val = corm_get(wrap->hd, key_buf);
    if (!val) {
        napi_value null_val;
        napi_get_null(env, &null_val);
        return null_val;
    }

    napi_value res;
    napi_create_string_utf8(env, (const char*)val, NAPI_AUTO_LENGTH, &res);
    return res;
}

/* CormStore.del(storeObj, key) */
static napi_value CormDel(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_status status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 2) {
        napi_throw_type_error(env, NULL, "Expected (storeObj, key)");
        return NULL;
    }

    CormStoreWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Corm store is closed or invalid");
        return NULL;
    }

    char key_buf[128] = {0};
    size_t key_len = 0;
    napi_get_value_string_utf8(env, args[1], key_buf, sizeof(key_buf) - 1, &key_len);

    corm_del(wrap->hd, key_buf);

    napi_value res;
    napi_get_boolean(env, true, &res);
    return res;
}

/* CormStore.save() */
static napi_value CormSave(napi_env env, napi_callback_info info) {
    (void)env;
    (void)info;
    corm_save();
    napi_value res;
    napi_get_boolean(env, true, &res);
    return res;
}

/* CormStore.close(storeObj) */
static napi_value CormClose(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_status status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (status != napi_ok || argc < 1) {
        napi_throw_type_error(env, NULL, "Expected (storeObj)");
        return NULL;
    }

    corm_save();

    CormStoreWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (wrap && wrap->is_open) {
        corm_close(wrap->hd);
        wrap->is_open = false;
    }

    napi_value res;
    napi_get_boolean(env, true, &res);
    return res;
}


/* --- Native libjoint wrapper --- */
typedef struct {
    unsigned jd;
    bool is_open;
} JointWrapper;

static void JointFinalizer(napi_env env, void* finalize_data, void* finalize_hint) {
    (void)env;
    (void)finalize_hint;
    JointWrapper* wrap = (JointWrapper*)finalize_data;
    if (wrap) {
        if (wrap->is_open && wrap->jd != 0) {
            joint_close(wrap->jd);
            wrap->is_open = false;
        }
        free(wrap);
    }
}

static napi_value JointInit(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    char path_buf[256] = {0};
    if (argc >= 1) {
        size_t len = 0;
        napi_get_value_string_utf8(env, args[0], path_buf, sizeof(path_buf) - 1, &len);
    }

    unsigned jd = joint_init(path_buf[0] ? path_buf : NULL);
    if (jd == (unsigned)-1) {
        napi_throw_error(env, NULL, "Failed to initialize libjoint database");
        return NULL;
    }

    JointWrapper* wrap = (JointWrapper*)calloc(1, sizeof(JointWrapper));
    wrap->jd = jd;
    wrap->is_open = true;

    napi_value res;
    napi_create_object(env, &res);
    napi_wrap(env, res, wrap, JointFinalizer, NULL, NULL);
    return res;
}

static napi_value JointRecord(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 4) {
        napi_throw_type_error(env, NULL, "Expected (jointObj, id, startTs, endTs)");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    uint32_t id = 0;
    int64_t start_ts = 0, end_ts = 0;
    napi_get_value_uint32(env, args[1], &id);
    napi_get_value_int64(env, args[2], &start_ts);
    napi_get_value_int64(env, args[3], &end_ts);

    joint_start(wrap->jd, (time_t)start_ts, id);
    joint_stop(wrap->jd, (time_t)end_ts, id);

    napi_value res;
    napi_get_boolean(env, true, &res);
    return res;
}

static napi_value JointIntersect(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected (jointObj, startTs, endTs)");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    int64_t start_ts = 0, end_ts = 0;
    napi_get_value_int64(env, args[1], &start_ts);
    napi_get_value_int64(env, args[2], &end_ts);

    joint_cur_t cur = joint_iter(wrap->jd, (time_t)start_ts, (time_t)end_ts);

    napi_value out_arr;
    napi_create_array(env, &out_arr);

    uint32_t idx = 0;
    time_t min_t, max_t;
    unsigned count, who;
    while (joint_next(&min_t, &max_t, &count, &who, &cur)) {
        napi_value val;
        napi_create_uint32(env, who, &val);
        napi_set_element(env, out_arr, idx++, val);
    }

    return out_arr;
}

/* --- Multi-Axis Query & Cormr Kernel Operations --- */

/* CormrCandidateSet: Set algebra operations */
typedef struct {
    cormr_set_t* set;
} CormrSetWrapper;

static void CormrSetFinalizer(napi_env env, void* finalize_data, void* finalize_hint) {
    (void)env;
    (void)finalize_hint;
    CormrSetWrapper* wrap = (CormrSetWrapper*)finalize_data;
    if (wrap) {
        if (wrap->set) {
            cormr_set_free(wrap->set);
            wrap->set = NULL;
        }
        free(wrap);
    }
}

static napi_value CormrSetCreate(napi_env env, napi_callback_info info) {
    (void)info;
    cormr_set_t* s = cormr_set_new();
    if (!s) {
        napi_throw_error(env, NULL, "Failed to allocate cormr set");
        return NULL;
    }

    CormrSetWrapper* wrap = (CormrSetWrapper*)calloc(1, sizeof(CormrSetWrapper));
    wrap->set = s;

    napi_value result;
    napi_create_object(env, &result);
    napi_wrap(env, result, wrap, CormrSetFinalizer, NULL, NULL);
    return result;
}

static napi_value CormrSetPush(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    CormrSetWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->set) {
        napi_throw_error(env, NULL, "Invalid cormr set");
        return NULL;
    }

    uint32_t ref = 0;
    napi_get_value_uint32(env, args[1], &ref);
    cormr_set_push(wrap->set, ref);

    napi_value res;
    napi_get_undefined(env, &res);
    return res;
}

static napi_value CormrSetSeal(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    CormrSetWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->set) {
        napi_throw_error(env, NULL, "Invalid cormr set");
        return NULL;
    }

    cormr_set_seal(wrap->set);

    napi_value res;
    napi_get_undefined(env, &res);
    return res;
}

static napi_value CormrSetToArray(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    CormrSetWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->set) {
        napi_throw_error(env, NULL, "Invalid cormr set");
        return NULL;
    }

    size_t count = cormr_set_count(wrap->set);
    const cormr_ref_t* refs = cormr_set_at(wrap->set);

    napi_value arr;
    napi_create_array_with_length(env, count, &arr);
    for (size_t i = 0; i < count; i++) {
        napi_value val;
        napi_create_uint32(env, refs[i], &val);
        napi_set_element(env, arr, (uint32_t)i, val);
    }
    return arr;
}

/* Set Algebra Operations: Intersect, Union, Subtract */
static napi_value CormrSetIntersect(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    CormrSetWrapper *wA = NULL, *wB = NULL;
    napi_unwrap(env, args[0], (void**)&wA);
    napi_unwrap(env, args[1], (void**)&wB);

    cormr_set_t* dst = cormr_set_new();
    cormr_set_intersect(dst, wA->set, wB->set);

    CormrSetWrapper* wrap = (CormrSetWrapper*)calloc(1, sizeof(CormrSetWrapper));
    wrap->set = dst;

    napi_value res;
    napi_create_object(env, &res);
    napi_wrap(env, res, wrap, CormrSetFinalizer, NULL, NULL);
    return res;
}

static napi_value CormrSetUnion(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    CormrSetWrapper *wA = NULL, *wB = NULL;
    napi_unwrap(env, args[0], (void**)&wA);
    napi_unwrap(env, args[1], (void**)&wB);

    cormr_set_t* dst = cormr_set_new();
    cormr_set_union(dst, wA->set, wB->set);

    CormrSetWrapper* wrap = (CormrSetWrapper*)calloc(1, sizeof(CormrSetWrapper));
    wrap->set = dst;

    napi_value res;
    napi_create_object(env, &res);
    napi_wrap(env, res, wrap, CormrSetFinalizer, NULL, NULL);
    return res;
}

static napi_value CormrSetSubtract(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    CormrSetWrapper *wA = NULL, *wB = NULL;
    napi_unwrap(env, args[0], (void**)&wA);
    napi_unwrap(env, args[1], (void**)&wB);

    cormr_set_t* dst = cormr_set_new();
    cormr_set_subtract(dst, wA->set, wB->set);

    CormrSetWrapper* wrap = (CormrSetWrapper*)calloc(1, sizeof(CormrSetWrapper));
    wrap->set = dst;

    napi_value res;
    napi_create_object(env, &res);
    napi_wrap(env, res, wrap, CormrSetFinalizer, NULL, NULL);
    return res;
}

/* Bounded Streaming Min-Heap Ranker */
typedef struct {
    cormr_rank_t* rk;
} CormrRankWrapper;

static void CormrRankFinalizer(napi_env env, void* finalize_data, void* finalize_hint) {
    (void)env;
    (void)finalize_hint;
    CormrRankWrapper* wrap = (CormrRankWrapper*)finalize_data;
    if (wrap) {
        if (wrap->rk) {
            cormr_rank_free(wrap->rk);
            wrap->rk = NULL;
        }
        free(wrap);
    }
}

static napi_value CormrRankCreate(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    uint32_t top_k = 10;
    if (argc >= 1) napi_get_value_uint32(env, args[0], &top_k);

    double min_score = 0.0;
    if (argc >= 2) napi_get_value_double(env, args[1], &min_score);

    cormr_rank_t* rk = cormr_rank_new(top_k, (float)min_score);
    if (!rk) {
        napi_throw_error(env, NULL, "Failed to allocate cormr rank buffer");
        return NULL;
    }

    CormrRankWrapper* wrap = (CormrRankWrapper*)calloc(1, sizeof(CormrRankWrapper));
    wrap->rk = rk;

    napi_value res;
    napi_create_object(env, &res);
    napi_wrap(env, res, wrap, CormrRankFinalizer, NULL, NULL);
    return res;
}

static napi_value CormrRankPush(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    CormrRankWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->rk) {
        napi_throw_error(env, NULL, "Invalid cormr rank handle");
        return NULL;
    }

    uint32_t ref = 0;
    napi_get_value_uint32(env, args[1], &ref);

    double score = 0.0;
    napi_get_value_double(env, args[2], &score);

    cormr_rank_push(wrap->rk, ref, (float)score);

    napi_value res;
    napi_get_undefined(env, &res);
    return res;
}

static napi_value CormrRankSorted(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    CormrRankWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->rk) {
        napi_throw_error(env, NULL, "Invalid cormr rank handle");
        return NULL;
    }

    uint32_t capacity = 100;
    if (argc >= 2) napi_get_value_uint32(env, args[1], &capacity);

    cormr_ref_t* refs = (cormr_ref_t*)malloc(capacity * sizeof(cormr_ref_t));
    float* scores = (float*)malloc(capacity * sizeof(float));

    size_t written = cormr_rank_sorted(wrap->rk, refs, scores);

    napi_value out_arr;
    napi_create_array_with_length(env, written, &out_arr);
    for (size_t i = 0; i < written; i++) {
        napi_value item;
        napi_create_object(env, &item);

        napi_value ref_val, score_val;
        napi_create_uint32(env, refs[i], &ref_val);
        napi_create_double(env, (double)scores[i], &score_val);

        napi_set_named_property(env, item, "ref", ref_val);
        napi_set_named_property(env, item, "score", score_val);
        napi_set_element(env, out_arr, (uint32_t)i, item);
    }

    free(refs);
    free(scores);
    return out_arr;
}

/* Async Work Context for Multi-Axis Background Execution */
typedef struct {
    napi_async_work work;
    napi_deferred deferred;
    cormr_ref_t* refs_in;
    size_t count_in;
    float* scores_in;
    size_t top_k;
    float min_score;
    cormr_ref_t* refs_out;
    float* scores_out;
    size_t count_out;
} AsyncQueryWorkContext;

static void AsyncQueryExecute(napi_env env, void* data) {
    (void)env;
    AsyncQueryWorkContext* ctx = (AsyncQueryWorkContext*)data;
    cormr_rank_t* rk = cormr_rank_new(ctx->top_k, ctx->min_score);
    if (rk) {
        for (size_t i = 0; i < ctx->count_in; i++) {
            cormr_rank_push(rk, ctx->refs_in[i], ctx->scores_in[i]);
        }
        ctx->refs_out = (cormr_ref_t*)malloc(ctx->top_k * sizeof(cormr_ref_t));
        ctx->scores_out = (float*)malloc(ctx->top_k * sizeof(float));
        ctx->count_out = cormr_rank_sorted(rk, ctx->refs_out, ctx->scores_out);
        cormr_rank_free(rk);
    }
}

static void AsyncQueryComplete(napi_env env, napi_status status, void* data) {
    AsyncQueryWorkContext* ctx = (AsyncQueryWorkContext*)data;
    if (status != napi_ok) {
        napi_reject_deferred(env, ctx->deferred, NULL);
    } else {
        napi_value arr;
        napi_create_array_with_length(env, ctx->count_out, &arr);
        for (size_t i = 0; i < ctx->count_out; i++) {
            napi_value item;
            napi_create_object(env, &item);

            napi_value ref_val, score_val;
            napi_create_uint32(env, ctx->refs_out[i], &ref_val);
            napi_create_double(env, (double)ctx->scores_out[i], &score_val);

            napi_set_named_property(env, item, "ref", ref_val);
            napi_set_named_property(env, item, "score", score_val);
            napi_set_element(env, arr, (uint32_t)i, item);
        }
        napi_resolve_deferred(env, ctx->deferred, arr);
    }

    if (ctx->refs_in) free(ctx->refs_in);
    if (ctx->scores_in) free(ctx->scores_in);
    if (ctx->refs_out) free(ctx->refs_out);
    if (ctx->scores_out) free(ctx->scores_out);
    napi_delete_async_work(env, ctx->work);
    free(ctx);
}

/* CormrRankAsync(refsArray, scoresArray, topK, minScore) -> Promise<results> */
static napi_value CormrRankAsync(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    uint32_t len = 0;
    napi_get_array_length(env, args[0], &len);

    AsyncQueryWorkContext* ctx = (AsyncQueryWorkContext*)calloc(1, sizeof(AsyncQueryWorkContext));
    ctx->count_in = len;
    ctx->refs_in = (cormr_ref_t*)malloc(len * sizeof(cormr_ref_t));
    ctx->scores_in = (float*)malloc(len * sizeof(float));

    for (uint32_t i = 0; i < len; i++) {
        napi_value ref_v, score_v;
        napi_get_element(env, args[0], i, &ref_v);
        napi_get_element(env, args[1], i, &score_v);

        napi_get_value_uint32(env, ref_v, &ctx->refs_in[i]);
        double s = 0.0;
        napi_get_value_double(env, score_v, &s);
        ctx->scores_in[i] = (float)s;
    }

    uint32_t top_k = 10;
    if (argc >= 3) napi_get_value_uint32(env, args[2], &top_k);
    ctx->top_k = top_k;

    double min_score = 0.0;
    if (argc >= 4) napi_get_value_double(env, args[3], &min_score);
    ctx->min_score = (float)min_score;

    napi_value promise;
    napi_create_promise(env, &ctx->deferred, &promise);

    napi_value work_name;
    napi_create_string_utf8(env, "corm_query_worker", NAPI_AUTO_LENGTH, &work_name);

    napi_create_async_work(env, NULL, work_name, AsyncQueryExecute, AsyncQueryComplete, ctx, &ctx->work);
    napi_queue_async_work(env, ctx->work);

    return promise;
}

/* Init module exports */
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"open", NULL, CormOpen, NULL, NULL, NULL, napi_default, NULL},
        {"put", NULL, CormPut, NULL, NULL, NULL, napi_default, NULL},
        {"get", NULL, CormGet, NULL, NULL, NULL, napi_default, NULL},
        {"del", NULL, CormDel, NULL, NULL, NULL, napi_default, NULL},
        {"save", NULL, CormSave, NULL, NULL, NULL, napi_default, NULL},
        {"close", NULL, CormClose, NULL, NULL, NULL, napi_default, NULL},
        {"jointInit", NULL, JointInit, NULL, NULL, NULL, napi_default, NULL},
        {"jointRecord", NULL, JointRecord, NULL, NULL, NULL, napi_default, NULL},
        {"jointIntersect", NULL, JointIntersect, NULL, NULL, NULL, napi_default, NULL},
        {"setCreate", NULL, CormrSetCreate, NULL, NULL, NULL, napi_default, NULL},
        {"setPush", NULL, CormrSetPush, NULL, NULL, NULL, napi_default, NULL},
        {"setSeal", NULL, CormrSetSeal, NULL, NULL, NULL, napi_default, NULL},
        {"setToArray", NULL, CormrSetToArray, NULL, NULL, NULL, napi_default, NULL},
        {"setIntersect", NULL, CormrSetIntersect, NULL, NULL, NULL, napi_default, NULL},
        {"setUnion", NULL, CormrSetUnion, NULL, NULL, NULL, napi_default, NULL},
        {"setSubtract", NULL, CormrSetSubtract, NULL, NULL, NULL, napi_default, NULL},
        {"rankCreate", NULL, CormrRankCreate, NULL, NULL, NULL, napi_default, NULL},
        {"rankPush", NULL, CormrRankPush, NULL, NULL, NULL, napi_default, NULL},
        {"rankSorted", NULL, CormrRankSorted, NULL, NULL, NULL, napi_default, NULL},
        {"rankAsync", NULL, CormrRankAsync, NULL, NULL, NULL, napi_default, NULL}
    };

    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
