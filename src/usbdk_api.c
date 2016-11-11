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
#include <config.h>

#include <windows.h>
#include <glib-object.h>
#include "usbdk_api.h"
#include "channel-usbredir-priv.h"

#define USB_DK_HIDE_RULE_MATCH_ALL ((ULONG64)(-1))
typedef struct tag_USB_DK_HIDE_RULE
{
    ULONG64 Hide;
    ULONG64 Class;
    ULONG64 VID;
    ULONG64 PID;
    ULONG64 BCD;
} USB_DK_HIDE_RULE, *PUSB_DK_HIDE_RULE;

typedef HANDLE(__cdecl *USBDK_CREATEHIDERHANDLE)(void);
typedef BOOL(__cdecl * USBDK_ADDHIDERULE)(HANDLE hider_handle, PUSB_DK_HIDE_RULE rule);
typedef BOOL(__cdecl *USBDK_CLEARHIDERULES)(HANDLE hider_handle);
typedef void(__cdecl *USBDK_CLOSEHIDERHANDLE)(HANDLE hider_handle);

struct tag_usbdk_api_wrapper
{
    HMODULE                                 module;
    USBDK_CREATEHIDERHANDLE                 CreateHandle;
    USBDK_ADDHIDERULE                       AddRule;
    USBDK_CLEARHIDERULES                    ClearRules;
    USBDK_CLOSEHIDERHANDLE                  CloseHiderHandle;
};

BOOL usbdk_is_driver_installed(void)
{
    gboolean usbdk_installed = FALSE;
    SC_HANDLE managerHandle = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);

    if (managerHandle) {
        SC_HANDLE serviceHandle = OpenService(managerHandle, TEXT("UsbDk"), GENERIC_READ);

        if (serviceHandle) {
            SPICE_DEBUG("UsbDk driver is installed.");
            usbdk_installed = TRUE;
            CloseServiceHandle(serviceHandle);
        }
        CloseServiceHandle(managerHandle);
    }
    return usbdk_installed;
}

void usbdk_api_unload(usbdk_api_wrapper *usbdk_api)
{
    if (usbdk_api->module != NULL) {
        SPICE_DEBUG("Unloading UsbDk API DLL");
        FreeLibrary(usbdk_api->module);
    }

    if (usbdk_api != NULL) {
        g_free(usbdk_api);
    }
}

BOOL usbdk_api_load(usbdk_api_wrapper **usbdk_api)
{
    *usbdk_api = g_new0(usbdk_api_wrapper, 1);

    SPICE_DEBUG("Loading UsbDk API DLL");
    (*usbdk_api)->module = LoadLibraryA("UsbDkHelper");
    if ((*usbdk_api)->module == NULL) {
        g_warning("Failed to load UsbDkHelper.dll, error %lu", GetLastError());
        goto error_unload;
    }

    (*usbdk_api)->CreateHandle = (USBDK_CREATEHIDERHANDLE)
        GetProcAddress((*usbdk_api)->module, "UsbDk_CreateHiderHandle");
    if ((*usbdk_api)->CreateHandle == NULL) {
        g_warning("Failed to find CreateHandle entry point");
        goto error_unload;
    }

    (*usbdk_api)->AddRule = (USBDK_ADDHIDERULE)
        GetProcAddress((*usbdk_api)->module, "UsbDk_AddHideRule");
    if ((*usbdk_api)->AddRule == NULL) {
        g_warning("Failed to find AddRule entry point");
        goto error_unload;
    }

    (*usbdk_api)->ClearRules = (USBDK_CLEARHIDERULES)
        GetProcAddress((*usbdk_api)->module, "UsbDk_ClearHideRules");
    if ((*usbdk_api)->ClearRules == NULL) {
        g_warning("Failed to find ClearRules entry point");
        goto error_unload;
    }

    (*usbdk_api)->CloseHiderHandle = (USBDK_CLOSEHIDERHANDLE)
        GetProcAddress((*usbdk_api)->module, "UsbDk_CloseHiderHandle");
    if ((*usbdk_api)->CloseHiderHandle == NULL) {
        g_warning("Failed to find CloseHiderHandle  entry point");
        goto error_unload;
    }
    return TRUE;

error_unload:
    usbdk_api_unload(*usbdk_api);
    return FALSE;
}

static uint64_t usbdk_usbredir_field_to_usbdk(int value)
{
    if (value >= 0)
        return value;
    else if (value == -1)
        return USB_DK_HIDE_RULE_MATCH_ALL;

    /* value is < -1 */
    g_return_val_if_reached(USB_DK_HIDE_RULE_MATCH_ALL);
}

static BOOL usbdk_add_hide_rule(usbdk_api_wrapper *usbdk_api,
                                HANDLE hider_handle,
                                PUSB_DK_HIDE_RULE rule)
{
    return usbdk_api->AddRule(hider_handle, rule);
}

void usbdk_api_set_hide_rules(usbdk_api_wrapper *usbdk_api, HANDLE hider_handle,
                              gchar *redirect_on_connect)
{
    struct usbredirfilter_rule *rules;
    int r, count;

    r = usbredirfilter_string_to_rules(redirect_on_connect, ",", "|",
                                       &rules, &count);
    if (r) {
        g_warning("auto-connect rules parsing failed with error %d", r);
        return;
    }

    for (int i = 0; i < count; i++) {
        USB_DK_HIDE_RULE rule;
        rule.Hide  = usbdk_usbredir_field_to_usbdk(rules[i].allow);
        rule.Class = usbdk_usbredir_field_to_usbdk(rules[i].device_class);
        rule.VID   = usbdk_usbredir_field_to_usbdk(rules[i].vendor_id);
        rule.PID   = usbdk_usbredir_field_to_usbdk(rules[i].product_id);
        rule.BCD   = usbdk_usbredir_field_to_usbdk(rules[i].device_version_bcd);
        if (usbdk_add_hide_rule(usbdk_api, hider_handle, &rule)) {
            SPICE_DEBUG("UsbDk add hide rule API failed");
        }
    }

    free(rules);
}

HANDLE usbdk_create_hider_handle(usbdk_api_wrapper *usbdk_api)
{
    return usbdk_api->CreateHandle();
}

BOOL usbdk_clear_hide_rules(usbdk_api_wrapper *usbdk_api, HANDLE hider_handle)
{
    return usbdk_api->ClearRules(hider_handle);
}

void usbdk_close_hider_handle(usbdk_api_wrapper *usbdk_api, HANDLE hider_handle)
{
    return usbdk_api->CloseHiderHandle(hider_handle);
}
