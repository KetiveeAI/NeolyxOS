/*
 * LyxStore JavaScript Bindings Implementation
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#include "lyxstore_bindings.h"
#include <stdlib.h>
#include <string.h>

/* Store reference for callbacks */
static lyx_store *g_store = NULL;

/* ============================================================================
 * LyxStore.search(query, options)
 * ============================================================================ */

static zs_value *js_store_search(zs_context *ctx, zs_value *this_val,
                                  int argc, zs_value **argv) {
    (void)this_val;
    if (!g_store || argc < 1) return zs_null(ctx);
    
    const char *query = zs_to_string(ctx, argv[0]);
    
    lyx_search_params params = {
        .query = query,
        .category = NULL,
        .offset = 0,
        .limit = 20,
        .sort_by = LYX_SORT_RELEVANCE
    };
    
    /* Parse options object if provided */
    if (argc > 1 && zs_is_object(argv[1])) {
        /* TODO: Parse options */
    }
    
    lyx_search_result *result = lyxstore_search_apps(g_store, params);
    if (!result) return zs_null(ctx);
    
    /* Convert to JS object */
    zs_object *obj = zs_object_create(ctx);
    zs_set_property(ctx, obj, "count", zs_integer(ctx, result->count));
    zs_set_property(ctx, obj, "total", zs_integer(ctx, result->total));
    
    /* Create apps array */
    zs_value *apps = zs_array_create(ctx, result->count);
    for (int i = 0; i < result->count; i++) {
        zs_object *app = zs_object_create(ctx);
        zs_set_property(ctx, app, "appId", zs_new_string(ctx, result->apps[i].app_id));
        zs_set_property(ctx, app, "name", zs_new_string(ctx, result->apps[i].name));
        zs_set_property(ctx, app, "version", zs_new_string(ctx, result->apps[i].version));
        zs_set_property(ctx, app, "author", zs_new_string(ctx, result->apps[i].author));
        zs_set_property(ctx, app, "description", zs_new_string(ctx, result->apps[i].description));
        zs_set_property(ctx, app, "category", zs_new_string(ctx, result->apps[i].category));
        zs_set_property(ctx, app, "rating", zs_number(ctx, result->apps[i].rating));
        zs_set_property(ctx, app, "isFree", zs_boolean(ctx, result->apps[i].is_free));
        
        zs_set_index(ctx, apps, i, (zs_value *)app);
    }
    zs_set_property(ctx, obj, "apps", apps);
    
    lyxstore_free_search_result(result);
    return (zs_value *)obj;
}

/* ============================================================================
 * LyxStore.getApp(appId)
 * ============================================================================ */

static zs_value *js_store_get_app(zs_context *ctx, zs_value *this_val,
                                   int argc, zs_value **argv) {
    (void)this_val;
    if (!g_store || argc < 1) return zs_null(ctx);
    
    const char *app_id = zs_to_string(ctx, argv[0]);
    lyx_app_info *info = lyxstore_get_app(g_store, app_id);
    
    if (!info) return zs_null(ctx);
    
    zs_object *app = zs_object_create(ctx);
    zs_set_property(ctx, app, "appId", zs_new_string(ctx, info->app_id));
    zs_set_property(ctx, app, "name", zs_new_string(ctx, info->name));
    zs_set_property(ctx, app, "version", zs_new_string(ctx, info->version));
    zs_set_property(ctx, app, "author", zs_new_string(ctx, info->author));
    zs_set_property(ctx, app, "description", zs_new_string(ctx, info->description));
    zs_set_property(ctx, app, "category", zs_new_string(ctx, info->category));
    zs_set_property(ctx, app, "rating", zs_number(ctx, info->rating));
    zs_set_property(ctx, app, "reviewCount", zs_integer(ctx, info->review_count));
    zs_set_property(ctx, app, "isFree", zs_boolean(ctx, info->is_free));
    zs_set_property(ctx, app, "price", zs_number(ctx, info->price));
    
    lyxstore_free_app_info(info);
    return (zs_value *)app;
}

/* ============================================================================
 * LyxStore.install(appId)
 * ============================================================================ */

static zs_value *js_store_install(zs_context *ctx, zs_value *this_val,
                                   int argc, zs_value **argv) {
    (void)this_val;
    if (!g_store || argc < 1) return zs_boolean(ctx, false);
    
    const char *app_id = zs_to_string(ctx, argv[0]);
    
    /* TODO: Support progress callback from JS */
    bool success = lyxstore_install_app(g_store, app_id, NULL, NULL);
    
    return zs_boolean(ctx, success);
}

/* ============================================================================
 * LyxStore.uninstall(appId)
 * ============================================================================ */

static zs_value *js_store_uninstall(zs_context *ctx, zs_value *this_val,
                                     int argc, zs_value **argv) {
    (void)this_val;
    if (!g_store || argc < 1) return zs_boolean(ctx, false);
    
    const char *app_id = zs_to_string(ctx, argv[0]);
    bool success = lyxstore_uninstall_app(g_store, app_id);
    
    return zs_boolean(ctx, success);
}

/* ============================================================================
 * LyxStore.getInstalled()
 * ============================================================================ */

static zs_value *js_store_get_installed(zs_context *ctx, zs_value *this_val,
                                         int argc, zs_value **argv) {
    (void)this_val; (void)argc; (void)argv;
    if (!g_store) return zs_array_create(ctx, 0);
    
    int count = 0;
    lyx_app_info **apps = lyxstore_get_installed(g_store, &count);
    
    zs_value *arr = zs_array_create(ctx, count);
    
    for (int i = 0; i < count && apps && apps[i]; i++) {
        zs_object *app = zs_object_create(ctx);
        zs_set_property(ctx, app, "appId", zs_new_string(ctx, apps[i]->app_id));
        zs_set_property(ctx, app, "name", zs_new_string(ctx, apps[i]->name));
        zs_set_property(ctx, app, "version", zs_new_string(ctx, apps[i]->version));
        zs_set_index(ctx, arr, i, (zs_value *)app);
    }
    
    return arr;
}

/* ============================================================================
 * LyxStore.User.login(email, password)
 * ============================================================================ */

static zs_value *js_user_login(zs_context *ctx, zs_value *this_val,
                                int argc, zs_value **argv) {
    (void)this_val;
    if (!g_store || argc < 2) return zs_boolean(ctx, false);
    
    const char *email = zs_to_string(ctx, argv[0]);
    const char *password = zs_to_string(ctx, argv[1]);
    
    bool success = lyxstore_login(g_store, email, password);
    return zs_boolean(ctx, success);
}

/* ============================================================================
 * LyxStore.User.logout()
 * ============================================================================ */

static zs_value *js_user_logout(zs_context *ctx, zs_value *this_val,
                                 int argc, zs_value **argv) {
    (void)this_val; (void)argc; (void)argv;
    if (g_store) lyxstore_logout(g_store);
    return zs_undefined(ctx);
}

/* ============================================================================
 * LyxStore.User.isLoggedIn()
 * ============================================================================ */

static zs_value *js_user_is_logged_in(zs_context *ctx, zs_value *this_val,
                                       int argc, zs_value **argv) {
    (void)this_val; (void)argc; (void)argv;
    return zs_boolean(ctx, g_store && lyxstore_is_logged_in(g_store));
}

/* ============================================================================
 * LyxStore.Themes.getAll()
 * ============================================================================ */

static zs_value *js_themes_get_all(zs_context *ctx, zs_value *this_val,
                                    int argc, zs_value **argv) {
    (void)this_val; (void)argc; (void)argv;
    if (!g_store) return zs_array_create(ctx, 0);
    
    int count = 0;
    lyx_theme_info **themes = lyxstore_get_themes(g_store, false, &count);
    
    zs_value *arr = zs_array_create(ctx, count);
    
    for (int i = 0; i < count && themes && themes[i]; i++) {
        zs_object *theme = zs_object_create(ctx);
        zs_set_property(ctx, theme, "themeId", zs_new_string(ctx, themes[i]->theme_id));
        zs_set_property(ctx, theme, "name", zs_new_string(ctx, themes[i]->name));
        zs_set_property(ctx, theme, "author", zs_new_string(ctx, themes[i]->author));
        zs_set_property(ctx, theme, "isDark", zs_boolean(ctx, themes[i]->is_dark));
        zs_set_property(ctx, theme, "isFree", zs_boolean(ctx, themes[i]->is_free));
        zs_set_index(ctx, arr, i, (zs_value *)theme);
        
        lyxstore_free_theme_info(themes[i]);
    }
    free(themes);
    
    return arr;
}

/* ============================================================================
 * Module Initialization
 * ============================================================================ */

void lyxstore_init_bindings(zs_context *ctx, lyx_store *store) {
    if (!ctx) return;
    
    g_store = store;
    
    /* Create LyxStore global object */
    zs_object *lyxstore = zs_object_create(ctx);
    
    /* Main methods */
    zs_set_property(ctx, lyxstore, "search", 
        zs_function_create(ctx, js_store_search, "search", 2));
    zs_set_property(ctx, lyxstore, "getApp", 
        zs_function_create(ctx, js_store_get_app, "getApp", 1));
    zs_set_property(ctx, lyxstore, "install", 
        zs_function_create(ctx, js_store_install, "install", 1));
    zs_set_property(ctx, lyxstore, "uninstall", 
        zs_function_create(ctx, js_store_uninstall, "uninstall", 1));
    zs_set_property(ctx, lyxstore, "getInstalled", 
        zs_function_create(ctx, js_store_get_installed, "getInstalled", 0));
    
    /* User namespace */
    zs_object *user = zs_object_create(ctx);
    zs_set_property(ctx, user, "login", 
        zs_function_create(ctx, js_user_login, "login", 2));
    zs_set_property(ctx, user, "logout", 
        zs_function_create(ctx, js_user_logout, "logout", 0));
    zs_set_property(ctx, user, "isLoggedIn", 
        zs_function_create(ctx, js_user_is_logged_in, "isLoggedIn", 0));
    zs_set_property(ctx, lyxstore, "User", (zs_value *)user);
    
    /* Themes namespace */
    zs_object *themes = zs_object_create(ctx);
    zs_set_property(ctx, themes, "getAll", 
        zs_function_create(ctx, js_themes_get_all, "getAll", 0));
    zs_set_property(ctx, lyxstore, "Themes", (zs_value *)themes);
    
    /* Register on global */
    zs_set_property(ctx, zs_context_global(ctx), "LyxStore", (zs_value *)lyxstore);
}
