/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2014-2015 Red Hat, Inc.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.

  Authors:
    Dmitry Fleytman <dmitry@daynix.com>
    Kirill Moizik <kirill@daynix.com>
*/
#ifndef USBDK_HEADER
#define USBDK_HEADER

typedef struct tag_usbdk_api_wrapper usbdk_api_wrapper;

BOOL     usbdk_is_driver_installed(void);
HANDLE   usbdk_create_hider_handle(usbdk_api_wrapper *usbdk_api);
BOOL     usbdk_clear_hide_rules(usbdk_api_wrapper *usbdk_api, HANDLE hider_handle);
void     usbdk_close_hider_handle(usbdk_api_wrapper *usbdk_api, HANDLE hider_handle);
BOOL     usbdk_api_load(usbdk_api_wrapper **usbdk_api);
void     usbdk_api_unload(usbdk_api_wrapper *usbdk_api);
void     usbdk_api_set_hide_rules(usbdk_api_wrapper *usbdk_api, HANDLE hider_handle, gchar *redirect_on_connect);
#endif
