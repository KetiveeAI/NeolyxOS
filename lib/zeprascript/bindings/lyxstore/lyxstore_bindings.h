/*
 * LyxStore JavaScript Bindings
 * 
 * Exposes LyxStore marketplace API to JavaScript applications.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef LYXSTORE_BINDINGS_H
#define LYXSTORE_BINDINGS_H

#include "../../include/zeprascript.h"
#include "../../../lyxstore/include/lyxstore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize LyxStore bindings in JS context */
void lyxstore_init_bindings(zs_context *ctx, lyx_store *store);

/*
 * JavaScript API:
 * 
 * LyxStore.search(query, options) -> Promise<SearchResult>
 * LyxStore.getApp(appId) -> Promise<AppInfo>
 * LyxStore.install(appId, onProgress) -> Promise<boolean>
 * LyxStore.uninstall(appId) -> Promise<boolean>
 * LyxStore.getInstalled() -> AppInfo[]
 * LyxStore.checkUpdates() -> Promise<AppInfo[]>
 * 
 * LyxStore.Themes.getAll() -> Promise<ThemeInfo[]>
 * LyxStore.Themes.install(themeId) -> Promise<boolean>
 * LyxStore.Themes.apply(themeId) -> boolean
 * 
 * LyxStore.User.login(email, password) -> Promise<boolean>
 * LyxStore.User.logout() -> void
 * LyxStore.User.isLoggedIn() -> boolean
 * LyxStore.User.getInfo() -> UserInfo | null
 */

#ifdef __cplusplus
}
#endif

#endif /* LYXSTORE_BINDINGS_H */
