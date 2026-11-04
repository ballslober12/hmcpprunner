"""Shared tables of GML built-in variables. Generates gen/builtins.h."""

# per-instance built-in variables (name -> is_array)
INST_BUILTINS = {
    'x': 0, 'y': 0, 'xprevious': 0, 'yprevious': 0, 'xstart': 0, 'ystart': 0,
    'hspeed': 0, 'vspeed': 0, 'speed': 0, 'direction': 0, 'friction': 0,
    'gravity': 0, 'gravity_direction': 0,
    'sprite_index': 0, 'image_index': 0, 'image_speed': 0, 'image_xscale': 0, 'image_yscale': 0,
    'image_angle': 0, 'image_alpha': 0, 'image_blend': 0, 'visible': 0, 'solid': 0, 'depth': 0,
    'persistent': 0, 'id': 0, 'object_index': 0, 'mask_index': 0,
    'bbox_left': 0, 'bbox_right': 0, 'bbox_top': 0, 'bbox_bottom': 0,
    'sprite_width': 0, 'sprite_height': 0, 'sprite_xoffset': 0, 'sprite_yoffset': 0, 'image_number': 0,
    'alarm': 1,
    'path_index': 0, 'path_position': 0, 'path_positionprevious': 0, 'path_speed': 0,
    'path_scale': 0, 'path_orientation': 0, 'path_endaction': 0,
    'timeline_index': 0, 'timeline_position': 0, 'timeline_speed': 0,
}

# global built-in variables (name -> is_array)
GLOBAL_BUILTINS = {
    'room': 0, 'room_width': 0, 'room_height': 0, 'room_speed': 0, 'room_caption': 0,
    'room_persistent': 0,
    'view_xview': 1, 'view_yview': 1, 'view_wview': 1, 'view_hview': 1,
    'view_xport': 1, 'view_yport': 1, 'view_wport': 1, 'view_hport': 1,
    'view_visible': 1, 'view_angle': 1, 'view_object': 1, 'view_hborder': 1, 'view_vborder': 1,
    'view_hspeed': 1, 'view_vspeed': 1, 'view_current': 0, 'view_enabled': 0,
    'mouse_x': 0, 'mouse_y': 0, 'mouse_button': 0, 'mouse_lastbutton': 0,
    'keyboard_key': 0, 'keyboard_lastkey': 0, 'keyboard_lastchar': 0, 'keyboard_string': 0,
    'fps': 0, 'current_time': 0, 'working_directory': 0, 'program_directory': 0,
    'temp_directory': 0, 'game_id': 0, 'instance_count': 0, 'instance_id': 1,
    'background_color': 0, 'background_colour': 0, 'background_showcolor': 0, 'background_showcolour': 0,
    'background_visible': 1, 'background_foreground': 1, 'background_index': 1,
    'background_x': 1, 'background_y': 1, 'background_width': 1, 'background_height': 1,
    'background_htiled': 1, 'background_vtiled': 1, 'background_hspeed': 1, 'background_vspeed': 1,
    'background_alpha': 1, 'background_blend': 1, 'background_xscale': 1, 'background_yscale': 1,
    'score': 0, 'lives': 0, 'health': 0, 'show_score': 0, 'show_lives': 0, 'show_health': 0,
    'argument_count': 0, 'os_type': 0, 'os_device': 0, 'os_browser': 0, 'os_version': 0,
    'error_occurred': 0, 'error_last': 0, 'secure_mode': 0, 'cursor_sprite': 0,
    'transition_kind': 0, 'transition_steps': 0, 'application_surface': 0,
    'display_aa': 0, 'fps_real': 0, 'delta_time': 0,
    'webgl_enabled': 0, 'async_load': 0, 'event_type': 0, 'event_number': 0, 'event_object': 0,
    'event_action': 0, 'game_display_name': 0, 'room_first': 0, 'room_last': 0,
    'debug_mode': 0, 'gamemaker_registered': 0, 'gamemaker_pro': 0, 'gamemaker_version': 0,
    'iap_data': 0, 'xboxone_': 0,
}

CONSTANTS = {
    # colours
    'c_white': 16777215, 'c_black': 0, 'c_red': 255, 'c_lime': 65280, 'c_green': 32768, 'c_blue': 16711680,
    'c_yellow': 65535, 'c_aqua': 16776960, 'c_fuchsia': 16711935, 'c_gray': 8421504, 'c_grey': 8421504,
    'c_ltgray': 12632256, 'c_ltgrey': 12632256, 'c_dkgray': 4210752, 'c_dkgrey': 4210752,
    'c_maroon': 128, 'c_navy': 8388608, 'c_olive': 32896, 'c_orange': 4235519, 'c_purple': 8388736,
    'c_silver': 12632256, 'c_teal': 8421376,
    # misc
    'pi': 3.141592653589793, 'true': 1, 'false': 0, 'noone': -4, 'all': -3, 'self': -1, 'other': -2, 'global': -5,
    'undefined': 0,
    # keys
    'vk_nokey': 0, 'vk_anykey': 1, 'vk_backspace': 8, 'vk_tab': 9, 'vk_enter': 13, 'vk_return': 13, 'vk_shift': 16,
    'vk_control': 17, 'vk_alt': 18, 'vk_pause': 19, 'vk_escape': 27, 'vk_space': 32, 'vk_pageup': 33,
    'vk_pagedown': 34, 'vk_end': 35, 'vk_home': 36, 'vk_left': 37, 'vk_up': 38, 'vk_right': 39, 'vk_down': 40,
    'vk_printscreen': 44, 'vk_insert': 45, 'vk_delete': 46, 'vk_numpad0': 96, 'vk_numpad1': 97, 'vk_numpad2': 98,
    'vk_numpad3': 99, 'vk_numpad4': 100, 'vk_numpad5': 101, 'vk_numpad6': 102, 'vk_numpad7': 103,
    'vk_numpad8': 104, 'vk_numpad9': 105, 'vk_multiply': 106, 'vk_add': 107, 'vk_subtract': 109,
    'vk_decimal': 110, 'vk_divide': 111, 'vk_f1': 112, 'vk_f2': 113, 'vk_f3': 114, 'vk_f4': 115, 'vk_f5': 116,
    'vk_f6': 117, 'vk_f7': 118, 'vk_f8': 119, 'vk_f9': 120, 'vk_f10': 121, 'vk_f11': 122, 'vk_f12': 123,
    'vk_lshift': 160, 'vk_rshift': 161, 'vk_lcontrol': 162, 'vk_rcontrol': 163, 'vk_lalt': 164, 'vk_ralt': 165,
    # mouse
    'mb_any': -1, 'mb_none': 0, 'mb_left': 1, 'mb_right': 2, 'mb_middle': 3,
    # blend modes
    'bm_normal': 0, 'bm_add': 1, 'bm_max': 2, 'bm_subtract': 3,
    'bm_zero': 1, 'bm_one': 2, 'bm_src_colour': 3, 'bm_inv_src_colour': 4, 'bm_src_alpha': 5,
    'bm_inv_src_alpha': 6, 'bm_dest_alpha': 7, 'bm_inv_dest_alpha': 8, 'bm_dest_colour': 9,
    'bm_inv_dest_colour': 10, 'bm_src_alpha_sat': 11,
    # text align
    'fa_left': 0, 'fa_center': 1, 'fa_right': 2, 'fa_top': 0, 'fa_middle': 1, 'fa_bottom': 2,
    # file attrs / dll
    'dll_cdecl': 0, 'dll_stdcall': 1, 'ty_real': 0, 'ty_string': 1,
    # events
    'ev_create': 0, 'ev_destroy': 1, 'ev_alarm': 2, 'ev_step': 3, 'ev_collision': 4, 'ev_keyboard': 5,
    'ev_mouse': 6, 'ev_other': 7, 'ev_draw': 8, 'ev_keypress': 9, 'ev_keyrelease': 10, 'ev_trigger': 11,
    'ev_step_normal': 0, 'ev_step_begin': 1, 'ev_step_end': 2,
    'ev_gui': 64, 'ev_draw_pre': 76, 'ev_draw_post': 77,
    'ev_user0': 10, 'ev_user1': 11, 'ev_user2': 12, 'ev_user3': 13, 'ev_user4': 14, 'ev_user5': 15,
    'ev_user6': 16, 'ev_user7': 17, 'ev_user8': 18, 'ev_user9': 19, 'ev_user10': 20, 'ev_user11': 21,
    'ev_user12': 22, 'ev_user13': 23, 'ev_user14': 24, 'ev_user15': 25,
    'ev_game_start': 2, 'ev_game_end': 3, 'ev_room_start': 4, 'ev_room_end': 5, 'ev_animation_end': 7,
    'ev_outside': 0, 'ev_boundary': 1, 'ev_left_button': 0, 'ev_right_button': 1, 'ev_middle_button': 2,
    'ev_no_button': 3, 'ev_left_press': 4, 'ev_right_press': 5, 'ev_middle_press': 6, 'ev_left_release': 7,
    'ev_right_release': 8, 'ev_middle_release': 9, 'ev_mouse_enter': 10, 'ev_mouse_leave': 11,
    'ev_global_left_button': 50, 'ev_global_right_button': 51, 'ev_global_middle_button': 52,
    'ev_global_left_press': 53, 'ev_global_right_press': 54, 'ev_global_middle_press': 55,
    'ev_global_left_release': 56, 'ev_global_right_release': 57, 'ev_global_middle_release': 58,
    # paths
    'path_action_stop': 0, 'path_action_restart': 1, 'path_action_continue': 2, 'path_action_reverse': 3,
    # sound / misc
    'cr_none': -1, 'cr_arrow': -2, 'cr_default': 0,
    'pr_pointlist': 1, 'pr_linelist': 2, 'pr_linestrip': 3, 'pr_trianglelist': 4, 'pr_trianglestrip': 5,
    'pr_trianglefan': 6,
    'cmpfunc_always': 8, 'cull_noculling': 0,
    'os_windows': 0, 'os_win32': 0, 'os_linux': 5, 'os_macosx': 1, 'os_psvita': 14,
    'fa_readonly': 1, 'fa_hidden': 2, 'fa_sysfile': 4, 'fa_volumelabel': 8, 'fa_directory': 16, 'fa_archive': 32,
    'tf_point': 0, 'tf_linear': 1, 'tf_anisotropic': 2,
    'gp_face1': 32769, 'gp_face2': 32770, 'gp_face3': 32771, 'gp_face4': 32772,
    'gp_shoulderl': 32773, 'gp_shoulderr': 32774, 'gp_shoulderlb': 32775, 'gp_shoulderrb': 32776,
    'gp_select': 32777, 'gp_start': 32778, 'gp_stickl': 32779, 'gp_stickr': 32780,
    'gp_padu': 32781, 'gp_padd': 32782, 'gp_padl': 32783, 'gp_padr': 32784,
    'gp_axislh': 32785, 'gp_axislv': 32786, 'gp_axisrh': 32787, 'gp_axisrv': 32788,
    'ds_type_list': 2, 'se_none': 0,
}


def gen_header(path):
    L = ['// generated by tools/builtins.py', '#pragma once', 'enum BI : int {']
    for n in INST_BUILTINS:
        L.append(f'  BI_{n},')
    L.append('  BI__COUNT };')
    L.append('enum GB : int {')
    for n in GLOBAL_BUILTINS:
        L.append(f'  GB_{n},')
    L.append('  GB__COUNT };')
    L.append('// constants')
    for n, v in CONSTANTS.items():
        if n in ('true', 'false', 'undefined', 'self', 'other', 'global', 'all', 'noone'):
            continue
        L.append(f'constexpr double {n} = {float(v)!r};')
    L.append('constexpr double noone = -4.0, all = -3.0;')
    open(path, 'w').write('\n'.join(L) + '\n')
