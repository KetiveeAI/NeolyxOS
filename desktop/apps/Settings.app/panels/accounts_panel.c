/*
 * NeolyxOS Settings - Accounts Panel
 * 
 * User account management: users, login options.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "panel_common.h"

rx_view* accounts_panel_create(settings_ctx_t* ctx) {
    (void)ctx;
    
    rx_view* panel = vstack_new(SECTION_GAP);
    if (!panel) return NULL;
    
    /* Header */
    rx_text_view* header = panel_header("User Accounts");
    if (header) view_add_child(panel, (rx_view*)header);
    
    /* Current User Card */
    rx_view* current_card = settings_card("Current User");
    if (current_card) {
        rx_view* user_row = hstack_new(16.0f);
        if (user_row) {
            rx_text_view* avatar = text_view_new("[avatar]");
            if (avatar) {
                text_view_set_font_size(avatar, 48.0f);
                avatar->color = NX_COLOR_PRIMARY;
                view_add_child(user_row, (rx_view*)avatar);
            }
            
            rx_view* user_info = vstack_new(4.0f);
            if (user_info) {
                rx_text_view* username = text_view_new("admin");
                if (username) {
                    text_view_set_font_size(username, 20.0f);
                    username->font_weight = 600;
                    username->color = NX_COLOR_TEXT;
                    view_add_child(user_info, (rx_view*)username);
                }
                rx_text_view* user_type = settings_label("Administrator", true);
                if (user_type) view_add_child(user_info, (rx_view*)user_type);
                view_add_child(user_row, user_info);
            }
            view_add_child(current_card, user_row);
        }
        
        rx_button_view* change_pwd_btn = button_view_new("Change Password");
        if (change_pwd_btn) view_add_child(current_card, (rx_view*)change_pwd_btn);
        
        view_add_child(panel, current_card);
    }
    
    /* Other Users Card */
    rx_view* users_card = settings_card("All Users");
    if (users_card) {
        rx_view* user1 = hstack_new(12.0f);
        if (user1) {
            rx_text_view* u1_name = text_view_new("admin");
            rx_text_view* u1_type = settings_label("Administrator", true);
            if (u1_name) {
                u1_name->color = NX_COLOR_TEXT;
                view_add_child(user1, (rx_view*)u1_name);
            }
            if (u1_type) view_add_child(user1, (rx_view*)u1_type);
            view_add_child(users_card, user1);
        }
        
        rx_view* user2 = hstack_new(12.0f);
        if (user2) {
            rx_text_view* u2_name = text_view_new("guest");
            rx_text_view* u2_type = settings_label("Standard User", true);
            if (u2_name) {
                u2_name->color = NX_COLOR_TEXT;
                view_add_child(user2, (rx_view*)u2_name);
            }
            if (u2_type) view_add_child(user2, (rx_view*)u2_type);
            view_add_child(users_card, user2);
        }
        
        rx_view* user_actions = hstack_new(12.0f);
        if (user_actions) {
            rx_button_view* add_btn = button_view_new("Add User");
            rx_button_view* remove_btn = button_view_new("Remove User");
            if (add_btn) {
                add_btn->normal_color = NX_COLOR_SUCCESS;
                view_add_child(user_actions, (rx_view*)add_btn);
            }
            if (remove_btn) {
                remove_btn->normal_color = NX_COLOR_ERROR;
                view_add_child(user_actions, (rx_view*)remove_btn);
            }
            view_add_child(users_card, user_actions);
        }
        view_add_child(panel, users_card);
    }
    
    /* Login Options Card */
    rx_view* login_card = settings_card("Login Options");
    if (login_card) {
        rx_view* auto_login = setting_toggle("Automatic Login", false);
        if (auto_login) view_add_child(login_card, auto_login);
        
        rx_view* require_pwd = setting_toggle("Require Password on Wake", true);
        if (require_pwd) view_add_child(login_card, require_pwd);
        
        view_add_child(panel, login_card);
    }
    
    return panel;
}
