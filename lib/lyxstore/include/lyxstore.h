/*
 * LyxStore - NeolyxOS App Marketplace
 * 
 * App store, theme store, and in-app purchases.
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 * PROPRIETARY AND CONFIDENTIAL
 */

#ifndef LYXSTORE_H
#define LYXSTORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Store Types
 * ============================================================================ */

typedef struct lyx_store lyx_store;

/* App info */
typedef struct lyx_app_info {
    const char *app_id;        /* Unique app identifier */
    const char *name;          /* Display name */
    const char *version;       /* Version string */
    const char *author;        /* Developer name */
    const char *description;   /* Short description */
    const char *category;      /* App category */
    const char *icon_url;      /* Icon URL */
    size_t size_bytes;         /* Download size */
    float rating;              /* User rating (0-5) */
    int review_count;          /* Number of reviews */
    bool is_free;              /* Free or paid */
    float price;               /* Price if paid */
    const char *currency;      /* Currency code */
} lyx_app_info;

/* Theme info */
typedef struct lyx_theme_info {
    const char *theme_id;      /* Unique theme identifier */
    const char *name;          /* Display name */
    const char *author;        /* Creator name */
    const char *preview_url;   /* Preview image URL */
    const char *colors[8];     /* Color palette preview */
    bool is_dark;              /* Dark theme */
    bool is_free;              /* Free or paid */
    float price;               /* Price if paid */
} lyx_theme_info;

/* Install status */
typedef enum lyx_install_status {
    LYX_NOT_INSTALLED = 0,
    LYX_INSTALLED,
    LYX_UPDATE_AVAILABLE,
    LYX_INSTALLING,
    LYX_FAILED,
} lyx_install_status;

/* ============================================================================
 * Category Groups (Top-Level for UI/Recommendations)
 * ============================================================================ */

typedef enum lyx_category_group {
    LYX_GROUP_ALL = 0,
    LYX_GROUP_PRODUCTIVITY,     /* Work & productivity apps */
    LYX_GROUP_ENTERTAINMENT,    /* Fun & media */
    LYX_GROUP_SOCIAL,           /* Communication & social */
    LYX_GROUP_EDUCATION,        /* Learning & reference */
    LYX_GROUP_LIFESTYLE,        /* Daily life & wellness */
    LYX_GROUP_GRAPHICS,         /* Creative & design */
    LYX_GROUP_SYSTEM,           /* System tools & extensions */
    LYX_GROUP_COUNT
} lyx_category_group;

/* Group display names */
static const char *lyx_group_names[] = {
    "All", "Productivity", "Entertainment", "Social",
    "Education", "Lifestyle", "Graphics", "System"
};

/* ============================================================================
 * Subcategories (Detailed categories within groups)
 * ============================================================================ */

typedef enum lyx_subcategory {
    LYX_SUBCAT_NONE = 0,
    
    /* Productivity (LYX_GROUP_PRODUCTIVITY) */
    LYX_SUBCAT_OFFICE,              /* Documents, Spreadsheets */
    LYX_SUBCAT_NOTES,               /* Note-taking apps */
    LYX_SUBCAT_BUSINESS,            /* CRM, Enterprise */
    LYX_SUBCAT_DEVELOPER_TOOLS,     /* IDEs, Git, Debugging */
    LYX_SUBCAT_UTILITIES,           /* Calculators, Converters */
    LYX_SUBCAT_FINANCE,             /* Banking, Crypto, Stocks */
    
    /* Entertainment (LYX_GROUP_ENTERTAINMENT) */
    LYX_SUBCAT_VIDEO,               /* Movies, TV, Streaming */
    LYX_SUBCAT_GAMES,               /* All games */
    LYX_SUBCAT_MUSIC,               /* Music players, Creation */
    LYX_SUBCAT_PHOTO,               /* Photo editors, Cameras */
    
    /* Social (LYX_GROUP_SOCIAL) */
    LYX_SUBCAT_SOCIAL_NETWORKS,     /* Social media */
    LYX_SUBCAT_MESSAGING,           /* Chat, Email */
    LYX_SUBCAT_NEWS,                /* News readers, Magazines */
    
    /* Education (LYX_GROUP_EDUCATION) */
    LYX_SUBCAT_LEARNING,            /* Courses, Tutorials */
    LYX_SUBCAT_BOOKS,               /* E-readers, Audiobooks */
    LYX_SUBCAT_REFERENCE,           /* Dictionaries, Encyclopedias */
    LYX_SUBCAT_KIDS,                /* Kids education */
    
    /* Lifestyle (LYX_GROUP_LIFESTYLE) */
    LYX_SUBCAT_SHOPPING,            /* Shopping apps */
    LYX_SUBCAT_HEALTH_FITNESS,      /* Workout, Meditation */
    LYX_SUBCAT_TRAVEL,              /* Maps, Hotels, Flights */
    LYX_SUBCAT_FOOD_DRINK,          /* Recipes, Restaurants */
    LYX_SUBCAT_SPORTS,              /* Sports news, Tracking */
    LYX_SUBCAT_WEATHER,             /* Weather apps */
    
    /* Graphics (LYX_GROUP_GRAPHICS) */
    LYX_SUBCAT_DESIGN,              /* UI/UX, Vector graphics */
    LYX_SUBCAT_DRAWING,             /* Digital painting */
    LYX_SUBCAT_3D,                  /* 3D modeling */
    LYX_SUBCAT_VIDEO_EDITING,       /* Video production */
    
    /* System (LYX_GROUP_SYSTEM) */
    LYX_SUBCAT_EXTENSIONS,          /* System extensions */
    LYX_SUBCAT_SECURITY,            /* Security tools */
    LYX_SUBCAT_NETWORKING,          /* Network utilities */
    
    LYX_SUBCAT_COUNT
} lyx_subcategory;

/* Subcategory display names */
static const char *lyx_subcategory_names[] = {
    "None",
    /* Productivity */
    "Office", "Notes", "Business", "Developer Tools", "Utilities", "Finance",
    /* Entertainment */
    "Video", "Games", "Music", "Photo",
    /* Social */
    "Social Networks", "Messaging", "News",
    /* Education */
    "Learning", "Books", "Reference", "Kids",
    /* Lifestyle */
    "Shopping", "Health & Fitness", "Travel", "Food & Drink", "Sports", "Weather",
    /* Graphics */
    "Design", "Drawing", "3D", "Video Editing",
    /* System */
    "Extensions", "Security", "Networking"
};

/* Map subcategory to parent group */
static const lyx_category_group lyx_subcat_to_group[] = {
    LYX_GROUP_ALL,  /* NONE */
    /* Productivity */
    LYX_GROUP_PRODUCTIVITY, LYX_GROUP_PRODUCTIVITY, LYX_GROUP_PRODUCTIVITY,
    LYX_GROUP_PRODUCTIVITY, LYX_GROUP_PRODUCTIVITY, LYX_GROUP_PRODUCTIVITY,
    /* Entertainment */
    LYX_GROUP_ENTERTAINMENT, LYX_GROUP_ENTERTAINMENT, LYX_GROUP_ENTERTAINMENT, LYX_GROUP_ENTERTAINMENT,
    /* Social */
    LYX_GROUP_SOCIAL, LYX_GROUP_SOCIAL, LYX_GROUP_SOCIAL,
    /* Education */
    LYX_GROUP_EDUCATION, LYX_GROUP_EDUCATION, LYX_GROUP_EDUCATION, LYX_GROUP_EDUCATION,
    /* Lifestyle */
    LYX_GROUP_LIFESTYLE, LYX_GROUP_LIFESTYLE, LYX_GROUP_LIFESTYLE,
    LYX_GROUP_LIFESTYLE, LYX_GROUP_LIFESTYLE, LYX_GROUP_LIFESTYLE,
    /* Graphics */
    LYX_GROUP_GRAPHICS, LYX_GROUP_GRAPHICS, LYX_GROUP_GRAPHICS, LYX_GROUP_GRAPHICS,
    /* System */
    LYX_GROUP_SYSTEM, LYX_GROUP_SYSTEM, LYX_GROUP_SYSTEM
};

/* Helper: get group for subcategory */
static inline lyx_category_group lyx_get_group(lyx_subcategory subcat) {
    return (subcat < LYX_SUBCAT_COUNT) ? lyx_subcat_to_group[subcat] : LYX_GROUP_ALL;
}

/* ============================================================================
 * Install Scope
 * ============================================================================ */

typedef enum lyx_install_scope {
    LYX_SCOPE_USER = 0,         /* Per-user installation (~/Applications) */
    LYX_SCOPE_SYSTEM,           /* System-wide (/Applications) - requires admin */
} lyx_install_scope;

/* ============================================================================
 * App Types
 * ============================================================================ */

typedef enum lyx_app_type {
    LYX_TYPE_APP = 0,           /* Standard application */
    LYX_TYPE_GAME,              /* Game */
    LYX_TYPE_WIDGET,            /* Desktop widget */
    LYX_TYPE_EXTENSION,         /* System extension */
    LYX_TYPE_THEME,             /* Theme pack */
    LYX_TYPE_FONT,              /* Font pack */
    LYX_TYPE_ICON_PACK,         /* Icon pack */
} lyx_app_type;

/* ============================================================================
 * Age Ratings
 * ============================================================================ */

typedef enum lyx_age_rating {
    LYX_RATING_EVERYONE = 0,    /* 4+ */
    LYX_RATING_9_PLUS,          /* 9+ */
    LYX_RATING_12_PLUS,         /* 12+ */
    LYX_RATING_17_PLUS,         /* 17+ */
} lyx_age_rating;

/* ============================================================================
 * Sort Options
 * ============================================================================ */

typedef enum lyx_sort_by {
    LYX_SORT_RELEVANCE = 0,     /* Best match for query */
    LYX_SORT_RATING,            /* Highest rated first */
    LYX_SORT_DOWNLOADS,         /* Most popular first */
    LYX_SORT_NEWEST,            /* Recently added */
    LYX_SORT_UPDATED,           /* Recently updated */
    LYX_SORT_NAME_ASC,          /* A-Z */
    LYX_SORT_NAME_DESC,         /* Z-A */
    LYX_SORT_PRICE_LOW,         /* Cheapest first */
    LYX_SORT_PRICE_HIGH,        /* Most expensive first */
    LYX_SORT_SIZE,              /* Smallest first */
} lyx_sort_by;

/* ============================================================================
 * Extended App Info
 * ============================================================================ */

typedef struct lyx_app_info {
    /* Basic info */
    const char *app_id;         /* Unique app identifier */
    const char *name;           /* Display name */
    const char *version;        /* Version string */
    const char *author;         /* Developer name */
    const char *author_id;      /* Developer ID */
    const char *description;    /* Short description */
    const char *full_description; /* Full description with markdown */
    
    /* Classification (Hierarchical) */
    lyx_category_group group;   /* Top-level category group */
    lyx_subcategory subcategory;/* Detailed subcategory */
    lyx_app_type type;          /* App type */
    lyx_age_rating age_rating;  /* Content rating */
    const char **tags;          /* Search tags */
    int tag_count;
    
    /* Media */
    const char *icon_url;       /* App icon URL */
    const char **screenshots;   /* Screenshot URLs */
    int screenshot_count;
    const char *video_url;      /* Preview video URL */
    
    /* Stats */
    float rating;               /* User rating (0-5) */
    int review_count;           /* Number of reviews */
    int download_count;         /* Total downloads */
    
    /* Pricing */
    bool is_free;               /* Free or paid */
    float price;                /* Price if paid */
    const char *currency;       /* Currency code (USD, EUR, etc.) */
    bool has_iap;               /* Has in-app purchases */
    
    /* Technical */
    size_t size_bytes;          /* Download size */
    const char *min_os_version; /* Minimum OS version required */
    const char **permissions;   /* Required permissions */
    int permission_count;
    lyx_install_scope install_scope; /* USER or SYSTEM level */
    
    /* Security & Integrity */
    const char *checksum_sha256;/* SHA-256 hash for verification */
    const char *signature;      /* Developer signature */
    bool is_verified;           /* Verified by LyxStore */
    
    /* Dates */
    const char *release_date;   /* Initial release */
    const char *update_date;    /* Last update */
    const char *changelog;      /* What's new */
    
    /* Links */
    const char *website_url;    /* Developer website */
    const char *support_url;    /* Support page */
    const char *privacy_url;    /* Privacy policy */
} lyx_app_info;

/* ============================================================================
 * Search Parameters (Enhanced)
 * ============================================================================ */

typedef struct lyx_search_params {
    /* Query */
    const char *query;              /* Search query text */
    
    /* Filters (Hierarchical) */
    lyx_category_group group;       /* Filter by category group (or ALL) */
    lyx_subcategory subcategory;    /* Filter by specific subcategory (or NONE) */
    lyx_app_type type;              /* Filter by app type */
    lyx_age_rating max_age;         /* Maximum age rating */
    bool free_only;                 /* Only free apps */
    bool no_iap;                    /* No in-app purchases */
    bool verified_only;             /* Only verified apps */
    const char *author_id;          /* Filter by developer */
    
    /* Sorting */
    lyx_sort_by sort_by;            /* Sort order */
    bool sort_descending;           /* Reverse sort */
    
    /* Pagination */
    int offset;                     /* Results offset */
    int limit;                      /* Results per page (max 50) */
} lyx_search_params;

typedef struct lyx_search_result {
    /* Results */
    lyx_app_info *apps;             /* Array of matching apps */
    int count;                      /* Number of results in this page */
    
    /* Metadata for UI ("Showing X of Y apps") */
    int total_available;            /* Total matching apps */
    int offset;                     /* Current offset */
    int limit;                      /* Requested limit */
    
    /* Query info */
    const char *corrected_query;    /* Spell-corrected query (or NULL) */
    float query_time_ms;            /* Query execution time */
} lyx_search_result;

/* ============================================================================
 * Store Lifecycle
 * ============================================================================ */

/* Initialize store connection */
lyx_store *lyxstore_init(const char *api_base_url);

/* Shutdown store */
void lyxstore_shutdown(lyx_store *store);

/* Check if store is connected */
bool lyxstore_is_connected(lyx_store *store);

/* ============================================================================
 * Category Queries
 * ============================================================================ */

/* Get all category groups */
const char **lyxstore_get_categories(int *count);

/* Get subcategories for a group */
const char **lyxstore_get_subcategories(lyx_category_group group, int *count);

/* Get apps by category group */
lyx_search_result *lyxstore_get_by_category(lyx_store *store, lyx_category_group group, 
                                             lyx_sort_by sort, int offset, int limit);

/* Get category group app count */
int lyxstore_get_category_count(lyx_store *store, lyx_category_group group);

/* ============================================================================
 * App Catalog
 * ============================================================================ */

lyx_search_result *lyxstore_search_apps(lyx_store *store, lyx_search_params params);
void lyxstore_free_search_result(lyx_search_result *result);

/* Get featured apps */
lyx_app_info **lyxstore_get_featured(lyx_store *store, int *count);

/* Get app details */
lyx_app_info *lyxstore_get_app(lyx_store *store, const char *app_id);
void lyxstore_free_app_info(lyx_app_info *info);

/* Get app updates */
lyx_app_info **lyxstore_check_updates(lyx_store *store, int *count);

/* ============================================================================
 * Theme Store
 * ============================================================================ */

/* Get available themes */
lyx_theme_info **lyxstore_get_themes(lyx_store *store, bool dark_only, int *count);

/* Get theme details */
lyx_theme_info *lyxstore_get_theme(lyx_store *store, const char *theme_id);
void lyxstore_free_theme_info(lyx_theme_info *info);

/* ============================================================================
 * Installation
 * ============================================================================ */

/* Install callback */
typedef void (*lyx_progress_fn)(float progress, const char *status, void *user_data);

/* Install app */
bool lyxstore_install_app(lyx_store *store, const char *app_id, 
                           lyx_progress_fn progress, void *user_data);

/* Update app */
bool lyxstore_update_app(lyx_store *store, const char *app_id,
                          lyx_progress_fn progress, void *user_data);

/* Uninstall app */
bool lyxstore_uninstall_app(lyx_store *store, const char *app_id);

/* Get install status */
lyx_install_status lyxstore_get_status(lyx_store *store, const char *app_id);

/* Install theme */
bool lyxstore_install_theme(lyx_store *store, const char *theme_id);

/* Uninstall theme */
bool lyxstore_uninstall_theme(lyx_store *store, const char *theme_id);

/* ============================================================================
 * User Account
 * ============================================================================ */

/* Login/logout */
bool lyxstore_login(lyx_store *store, const char *email, const char *password);
void lyxstore_logout(lyx_store *store);
bool lyxstore_is_logged_in(lyx_store *store);

/* Get user info */
typedef struct lyx_user_info {
    /* Identity */
    const char *user_id;        /* Unique user ID */
    const char *email;          /* Email address */
    const char *display_name;   /* Display name */
    const char *avatar_url;     /* Profile picture URL */
    
    /* Account status */
    bool is_verified;           /* Email verified */
    bool is_developer;          /* Developer account */
    const char *account_type;   /* "free", "premium", "developer" */
    const char *created_date;   /* Account creation date */
    
    /* Purchases */
    const char **purchased_apps;    /* Purchased app IDs */
    int purchased_count;
    const char **purchased_themes;  /* Purchased theme IDs */
    int purchased_theme_count;
    
    /* Preferences */
    lyx_category *favorite_categories;  /* Preferred categories */
    int favorite_cat_count;
    bool auto_update;           /* Auto-update apps */
    bool notifications_enabled; /* Push notifications */
    const char *locale;         /* Preferred locale (en-US, etc.) */
    const char *currency;       /* Preferred currency */
    
    /* Stats */
    int apps_installed;         /* Total apps installed */
    int reviews_submitted;      /* Reviews submitted */
    float avg_rating_given;     /* Average rating given */
    
    /* Linked accounts */
    bool has_payment_method;    /* Payment method on file */
    const char *country;        /* Country code */
} lyx_user_info;

lyx_user_info *lyxstore_get_user(lyx_store *store);
void lyxstore_free_user_info(lyx_user_info *info);

/* Update user preferences */
bool lyxstore_update_preferences(lyx_store *store, bool auto_update, 
                                  bool notifications, const char *locale);

/* Set favorite categories */
bool lyxstore_set_favorites(lyx_store *store, lyx_category *categories, int count);

/* ============================================================================
 * Purchases
 * ============================================================================ */

/* Purchase app */
bool lyxstore_purchase_app(lyx_store *store, const char *app_id);

/* Purchase theme */
bool lyxstore_purchase_theme(lyx_store *store, const char *theme_id);

/* Restore purchases */
bool lyxstore_restore_purchases(lyx_store *store);

/* ============================================================================
 * Reviews
 * ============================================================================ */

typedef struct lyx_review {
    const char *user_name;
    float rating;
    const char *text;
    const char *date;
} lyx_review;

/* Get reviews */
lyx_review **lyxstore_get_reviews(lyx_store *store, const char *app_id, int *count);

/* Submit review */
bool lyxstore_submit_review(lyx_store *store, const char *app_id, 
                             float rating, const char *text);

/* ============================================================================
 * Local App Management
 * ============================================================================ */

/* Get installed apps */
lyx_app_info **lyxstore_get_installed(lyx_store *store, int *count);

/* Get app installation path */
const char *lyxstore_get_app_path(lyx_store *store, const char *app_id);

/* Launch app */
bool lyxstore_launch_app(lyx_store *store, const char *app_id);

/* ============================================================================
 * ZebraScript Integration
 * ============================================================================ */

/* Register LyxStore APIs with JS context */
struct zs_context;
void lyxstore_register_js(lyx_store *store, struct zs_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LYXSTORE_H */
